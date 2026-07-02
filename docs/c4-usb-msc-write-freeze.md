# Colorfly C4 Pro — USB-MSC sustained-write freeze (FIXED)

**Status: FIXED and field-verified 2026-07-02 — see s7.1. Root mechanism was MEASURED
(s7): an AHB bus wedge from the UDC's bulk-OUT DMA engine, triggered by the upstream
rk27xx driver's receive-arm sequence. Fix: `C4PRO_USB_RX_DMA_GUARD` (default-on,
colorflyc4pro.h) — mirror the stock firmware's disassembled RX discipline in
`ep_read()`. Verification gate passed: 900+ MB copied on a freshly formatted volume
with zero freezes (previously: certain death within minutes).**

This document is kept as the full investigation record. It separates **field-tested
facts** (observed on real hardware) from **interpretation** (derived conclusions) and
from **hypotheses** (unconfirmed guesses). The bug survived ~20 build/flash/test
iterations across an earlier session; the original core blocker was that **we could
not capture where the CPU is spinning** (section 6). That blocker was resolved by the
JTAG-free watchdog crash capture (section 7), which measured the actual frozen program
counter and converged on the real mechanism; the stock-firmware disassembly then
provided the exact fix (section 7.1).

---

## 1. Problem statement — field-tested facts only

### Symptom
- Writing to the C4's USB mass-storage volume from a host PC eventually **hard-freezes
  the device**: display frozen or blank (backlight sometimes off), all buttons dead.
  Recovery requires a power-cycle, or a CE#-short into MaskROM + reflash.
- **Two independent, confirmed reproductions:**
  1. Copying a folder of WAV files (each ~20–50 MB). Freezes after **between 1 and
     several files** — variable.
  2. Host **quick-format** of the volume. Freezes during the format. (This is the
     faster reproduction.)
- The freeze point is **non-deterministic**. Across runs, the on-screen bytes-written
  counter at the freeze corresponded to roughly **12, 19, 25, 27, 37, 53, 79, 87, and
  94 MB** — no fixed threshold.

### What still works (facts)
- Cold boot, UI, and enumeration as a ~30 GB USB drive: work.
- **Reads / playback** of files that were successfully copied: work. Read throughput
  measured ~4.4 MB/s (our build) vs ~6.7 MB/s (stock).
- **Storage survives the freeze.** After a power-cycle the volume remounts (dirty
  full-scan ~30 s–2 min) and previously-written data is intact. The freeze by itself
  does not corrupt the owned volume.
- **Stock original firmware sustains writes at ~6.5 MB/s with no freeze.** ⇒ the
  hardware is capable; this is a firmware defect, not a hardware limit.

### On-screen counters at the moment of freeze (facts, read directly off the LCD)
A diagnostic build paints live counters. At the freeze, **consistently across many
runs**, they read:

| Counter | Meaning | Value at freeze |
|---|---|---|
| **T** | kernel tick (INT_TIMER0) | **FROZEN** (watched several seconds, not incrementing) |
| **U** | UI / MSC-serve loop | **FROZEN** at same instant as T |
| **D** | timer-drain (bumped in irq_handler when timer overflowed) | **FROZEN**, equal to T |
| **F** | CPU-fault-handler (UIE) entry count | **0** — abort/exception handler never entered |
| **R** | USB bus-reset count | **> 0 and had been INCREMENTING** (observed 2, 5, 14 in different runs) |
| **X** | last USB INT2FLAG (last UDC event) | **0x100** (bulk-OUT EP1 done) or **0x200** (bulk-IN EP2 done) |
| **P** | write-path phase marker | **12** = "CSW sent, thread parked awaiting next command" |
| **C** | cache-op spin-cap fire count | **0** — the capped CACHEOP loop never fired |
| **IRQ** | last unhandled IRQ source | **31 (0x1f)** = the known-benign "source already deasserted" spurious read |

Additional facts about P: **P=12 is displayed for a while WITH T/U/D still ticking
before the freeze** — i.e. P=12 is the normal resting state between commands, not the
place the CPU is stuck.

### One earlier, DIFFERENT failure (fact, on an earlier build)
Before the "UDC-ack" change (attempt #5 below), one run showed a **visible PANIC**
rather than a silent freeze:

```
PANIC: Unhandled IRQ 10: INT_UDC   pc:6006a610
```

- `10` is hex = **16** = INT_UDC (irqvector index 16).
- `pc 0x6006a610` maps (addr2line) to **`c4own_fill_superblock_page`** — the FTL
  superblock/checkpoint write inside the flush path.

⇒ At that instant the CPU was executing the FTL flush when an INT_UDC interrupt was
dispatched to the default (panic) handler. After making the unhandled-IRQ path ack
source 16 instead of panicking, this panic disappeared and the **silent** freeze
(section above) remained.

---

## 2. Interpretation of the signature (derived, not assumed)

This is a logical reading of the facts, not a guess, and it has been **stable across
every build**:

- **T, U, D freeze together** ⇒ the CPU stopped taking interrupts entirely (no ticks,
  no drain).
- **F = 0** ⇒ it is **not** a CPU data/prefetch abort.
- Therefore the CPU is in a **busy-spin with interrupts masked** — either inside an
  interrupt handler that never returns, or inside a `disable_irq`/critical section that
  never exits.

The exact spinning instruction/loop has **never been located by measurement.**

---

## 3. What was tried, and the on-device result

Chronological. Each change is compile-flag-gated. ✅ = worked / kept, ❌ = did not fix
the freeze, ⚠️ = partial or side effect, ❓ = untested.

| # | Change | Goal | On-device result |
|---|---|---|---|
| 1 | NAND read-delay tuning + hardware BCH ECC | read speed | ✅ reads 1→4.4 MB/s. **Kept.** |
| 2 | Full 30 GB volume + multi-die fast format | capacity | ✅ **Kept.** |
| 3 | TIMER_PRIO via INTC SCR priority write | keep tick alive under IRQ load | ❌ **broke boot** (hung on logo). A DRAM probe proved the SCR write itself freezes the tick. Abandoned. |
| 4 | TIMER_PRIO via TMR0CON drain in irq_handler | keep tick alive without INTC write | ✅ boots; ❌ did **not** fix the freeze. **Kept** (harmless, keeps tick honest). |
| 5 | Ack source-16 in UIRQ instead of panicking | stop the "Unhandled IRQ INT_UDC" panic | ✅ panic gone; ❌ freeze persists (now silent). |
| 6 | Fault-visible UIE (force backlight, paint PC) + dedicated 4 KB abort stack | make a crash show its PC | ⚠️ **never showed a fault frame** ⇒ confirms F=0 ⇒ **not a fault**. |
| 7 | Eager flush — flush each completed block inline, keep only the tail | shorten end-of-file flush so host does not time out | ❌ freeze persists; ⚠️ **write speed collapsed to ~1 MB/s** (FAT/data write-amplification). |
| 8 | Pipelined flush — keep N=3 dirty blocks, flush oldest | restore speed + keep sync short | ⚠️ reset count dropped 14→2, copy got further; ❌ still freezes. **Kept.** |
| 9 | Cache-op spin cap (`while(CACHEOP&0x03)`) | cap a masked-IRQ spin | ❌ **cap never fired (C=0)** ⇒ not that spin. |
| 10 | Standard IRQ-driven transfer (bypass the custom polled bulk-IN sender) | remove custom sender as a suspect | ❌ freeze persists ⇒ **not the sender**; ⚠️ added boot white-screen flakiness. **Reverted.** |
| 11 | DMA-abort on USBRST (clear DMA_START on all EPs + FIFO flush) | release a wedged DMA/AHB master after a reset | ⚠️ copy got further; ❌ still freezes. **Kept.** |
| 12 | Defer `usb_drv_init` out of IRQ + skip stale data-EP completions while `bus_reset_pending` | stop a stale bulk-OUT completion re-arming DMA during a reset | ❓ **flashed but NOT tested — the debugging was stopped here.** |

Several of these (1, 2, 4, 8, 11) are genuine improvements and are kept even though
they do not fix the freeze. The freeze remains after all of them.

---

## 4. Ruled OUT by field test
- ❌ **Not a CPU fault/exception** — F=0 on every build; abort handler never entered.
- ❌ **Not kernel-tick starvation** — the TMR0CON drain keeps the tick alive; D advances
  right up to the freeze. (This was the original hypothesis; disproven.)
- ❌ **Not the custom polled bulk-IN sender** — the standard IRQ-driven transfer path
  froze identically.
- ❌ **Not the CACHEOP cache-op spin** — its cap never fired (C=0).
- ❌ **Not (solely) end-of-file flush duration** — a short/eager flush did not stop it.
- ❌ **Not purely load-dependent** — a small host quick-format also freezes.
- ❌ **Does not corrupt storage** — the volume survives the power-cycle intact.

## 5. Established by field test
- ✅ It is a **busy-spin with interrupts masked** (T=D frozen, F=0).
- ✅ The host issues **repeated USB bus resets** during the transfer (R increments), and
  the freeze happens during/around this reset activity. *(Recontextualized by §5.1: the
  resets FOLLOW the wedge — they are the host's recovery attempts, not the trigger.)*
- ✅ The **last USB event before the freeze is a bulk endpoint completion** (X=0x100 /
  0x200) with the write thread parked (P=12).
- ✅ **Reducing device stalls reduces resets and gets further** — pipelined flush cut
  resets 14→2 and let the copy progress, so resets correlate with device slowness and
  surviving them helps.

---

## 5.1 Host-side usbmon capture, 2026-07-02 — MEASURED (first independent view)

Capture: `usbmon0` via dumpcap during a WAV-folder copy on the reset-defer build
(attempt #12). File preserved at `Colorfly-C4/captures/c4-usb-freeze-2026-07-02.pcapng`.
Device = bus 3, address 2, `071b:3203`.

Timeline (relative to capture start):
- t≈114 s — plug-in, clean HS enumeration; mount chatter (Test Unit Ready polls) until ~145 s.
- t=147.087–151.030 s — **208 sequential Write(10)×64-block commands, 6.4 MB, LBA
  0xbca0→0xefd0 contiguous** (pure file data, no FAT churn at death). ~10 NAND flushes
  worth of data — flushing had been working.
- t=151.030032 — CSW for write #207 delivered (device firmware alive).
- t=151.030069 — CBW for write #208 (Write(10) LBA 0xefd0) **received and ACKed** by the
  device (status 0).
- t=151.03→181.20 — host submits the 32 KB data-OUT; **device never takes one byte**
  (NAK forever). Host's 30 s SCSI timeout fires, URB cancelled (status −104).
- t=181.2 onward — hub PORT_RESET **succeeds at the hub level**, but the device **never
  answers GET DESCRIPTOR at address 0**; host repeats the reset+descriptor cycle 4-5×
  (t=186/192/197/202 s), every one times out (status −2); host disables the port.

**What this measures (facts, not inference):**
1. **The wedge is spontaneous** — NO bus event precedes it. No reset, no STALL, no
   babble, no unusual command. The device died mid-stream between ACKing a CBW and
   arming its data phase, at full write speed, with zero warning.
2. **The host resets are an EFFECT of the wedge, not the trigger.** The first PORT_RESET
   happens 30 s *after* the device stopped responding. This **falsifies the premise of
   attempts #10/#11/#12** (reset-guard, reset-DMA-abort, reset-defer/stale-guard): they
   harden a path that runs only after the device is already dead.
3. **A hardware USB bus reset does not un-wedge the CPU.** Answering GET DESCRIPTOR
   after reset needs only the UDC IRQ + EP0 path; it never answers → interrupts are
   permanently dead, confirming the masked-spin conclusion from outside.
4. The wedge window is tiny: firmware demonstrably alive at CSW #207 (it sent it and
   re-armed the CBW receive), dead by the time the #208 data phase needed arming.
   Matches the device-side X=0x100 marker (last logged IRQ = bulk-OUT/CBW completion).

**Code audit against this window (2026-07-02, read-only):** `INT_UDC`, `out_intr`,
`in_intr`, `ep_read` are loop-free; all bulk-IN polls are iteration-capped under
`C4PRO_USB_TX_WEDGE_GUARD`; FTL/NAND contain **no** `disable_irq` (their unbounded
`FLCTL & FL_RDY` polls at `nand-rk27xx.c:386,404` run IRQ-enabled, so alone they cannot
freeze the tick); `core_sleep()` is benign (enable_irq only, power-down commented out).
An IRQ-refire storm is excluded by the frozen D counter (the TMR0CON drain in the IRQ
asm would keep counting). Remaining shapes that fit: a corrupted IRQ dispatch target
(e.g. DMA overwriting `irqvector`/kernel state → PC loops in valid memory with I-bit
set, no fault), or an unaudited masked section reachable from the queue/wakeup path on
the bulk-OUT completion.

~~**Corrected premise — post-mortem RAM is probably READABLE.**~~ **FALSIFIED by direct
test, 2026-07-02:** a DRAM memdump payload run via CE#-short + s2run (same RAM path as
the NAND rawdump harness) came back ~99.5% decayed garbage, twice, matching only the
DRAM cells' own bias pattern (0xcc/0x33 repeating). The *human* replug cycle is slow
enough (tens of seconds with no refresh) that content is lost — this premise was wrong.
The correct version of this idea, using a **hardware** reset instead of a human one, is
what worked; see s7.

## 5.2 Host dmesg correlation, 2026-07-02 — confirms the wedge is total, not partial

`sudo dmesg` around a later repro (converted from kernel uptime to wall-clock and
matched against the pcap's absolute frame timestamps) showed the full host-side
recovery ladder, in order:
1. `usb 3-4: reset high-speed USB device` — the host's own ~30 s SCSI command timeout
   (matching s5.1) fires a port reset.
2. `usb 3-4: device descriptor read/64, error -110` (ETIMEDOUT) — **twice**, ~5.5 s
   apart. The device does not answer even a bare `GET_DESCRIPTOR` at address 0 after the
   reset — i.e. it is deaf to standard USB control transfers, not just to the SCSI/MSC
   protocol on top.
3. `usb 3-4: USB disconnect` — after the second failed re-enumeration attempt, the USB
   core itself (not the SCSI layer) gives up and tears the device down.
4. `device offline error` / `Buffer I/O error` / `FAT-fs ... bread failed` — the block
   layer erroring every queued I/O against the now-gone device, cascading into the host
   filesystem remounting read-only.

This matters because it rules out a narrower theory floated mid-investigation (that the
CPU might have already recovered by the time the host notices, and the failure is
purely the host's own recovery-budget/timeout logic giving up "too early"). A device
that only *seemed* stuck but was actually idling normally again would still answer a
bare `GET_DESCRIPTOR` — that request needs nothing but EP0 and the UDC IRQ path. Total,
repeated silence to it confirms the device genuinely is not processing interrupts at
all during this window, consistent with s7's bus-stall measurement, not merely running
behind.

---

## 6. The real blocker — we cannot see WHERE it spins

This is the honest reason 20 iterations did not converge:

- At the freeze, interrupts are masked and the CPU is spinning, so the on-screen
  heartbeat is **frozen at its last painted frame** — it cannot display anything new,
  including a marker updated inside the spinning region.
- The CPU-fault path (UIE) is never entered (F=0), so the fault-visible screen never
  fires.
- ~~RAM is lost on the power-cycle needed to recover~~ **WRONG — see §5.1:** CE#-short
  recovery keeps power; the MaskROM rawdump harness can read the wedged DRAM post-mortem.
- There is **no debugger / JTAG / serial console** attached.

Net effect: every "which loop is it" question was answered by **inference and code
audit, not direct measurement** — which is exactly why capped loops (#9, #11) kept
turning out to be the wrong loop.

### What would actually get the data (do this BEFORE more fix attempts)
1. **Host-side USB capture (cheapest, no hardware mod).** Run `usbmon` + Wireshark (or
   `sudo modprobe usbmon`) on the PC during a copy/format. This gives the **host's
   authoritative view**: the exact SCSI command in flight, when each bus reset is
   issued and why (stall? timeout? NAK storm?), and the last successful transfer before
   the device stops responding. This is the first *independent* view of the failure and
   would confirm/deny the reset trigger and its cause without needing device-side
   visibility.
2. **Hardware watchdog + a reset-surviving crash log (device-side equivalent).** Arm the
   RK27xx WDT and pet it from a tick task. When the CPU wedges (tick frozen), the WDT
   fires and resets the chip. The key addition is stashing "last phase / last function
   id / last PC" into a location that **survives the WDT reset** — an SoC scratch
   register that is not cleared by a watchdog reset, or a reserved DRAM/NAND region
   re-read early in the next boot — and printing it on the next boot. This turns the
   invisible spin into a readable address. Highest-value device-side step **if** a
   survivable scratch location exists (needs verifying against the RK27xx datasheet /
   stock disassembly).
3. **Serial/UART console**, if any UART pad is exposed on the PCB — `logf()` output would
   stream the last driver activity in real time up to the wedge.
4. **JTAG/SWD**, if the pads are accessible — halt the core after the freeze and read the
   PC directly. Definitive; ends the guessing entirely.

Recommended order: **(1) first** (free, immediate, independent), then **(2)** if a
scratch survivor exists, then hardware (3)/(4) if still unresolved.

---

## 7. Decisive measurement — the frozen PC, via WDT crash capture (2026-07-02)

The original blocker (section 6) is resolved. Mechanism: a hardware watchdog timer
(WDT) is independent of the CPU/tick and fires even when both are dead, unlike every
software-based recovery attempted earlier. Confirmed prerequisite by direct test: DRAM
content **does** survive a WDT-triggered SoC reset (unlike the slow human CE#-short
replug, which does not — see the correction in section 5.1). Design:

- `c4pro_wdt_capture_start()` (`system-rk27xx.c`) arms the WDT once, on entering the MSC
  serve loop.
- `INT_TIMER0` (`kernel-rk27xx.c`) pets the WDT every tick by reloading `WDTLR`, and in
  the same tick writes a magic value + the current interrupted PC (already captured on
  every IRQ entry by `irq_handler`'s `lr`-stash, `system-rk27xx.c`) into a fixed DRAM
  record (`0x60910000`).
- The instant the tick genuinely stops (the freeze itself), petting stops, the
  already-armed countdown expires on its own, and the chip resets with the last-written
  PC intact in DRAM.
- On the next boot, before anything else runs, `c4pro_usb_msc_serve()` checks the DRAM
  record for the magic value and displays the captured PC on screen if present.
  (`C4PRO_WDT_CRASH_CAPTURE` build flag.)

**Field note:** the first attempt used `WDTLR=1` (the exact value `system_reboot()`
already uses successfully elsewhere in this codebase) for the per-tick reload, and
caused an immediate boot loop — the countdown from 1 apparently completes in under one
tick period, so the WDT fired before even the first tick could write a record. Fixed by
reloading a much larger value (`50000000`, based on `tick_start()`'s documented 50 MHz
clock for the sibling TMR0 peripheral on the same APB0 bus — unverified for the WDT
specifically, but empirically confirmed to work: no more boot loop, and a real freeze
was caught cleanly on the very next repro).

**Captured on a genuine freeze during the WAV-copy repro:**
```
pc   = 0x60071e38   (raw lr_irq, i.e. interrupted-instruction-address + 4)
tick = 14984
```
Corrected for ARM7TDMI's IRQ return offset (`lr_irq = interrupted_pc + 4`, the same
offset `irq_handler` itself already subtracts via `subs pc, lr, #4`), the true
interrupted address is **`0x60071e34`**. Symbolicated against the exact matching build
(verified via disassembly before flashing, matching `WDTLR` constant present at both
call sites): this is the **last instruction of `enable_interrupt.constprop.0`**
(`thread.c`, a 4-instruction generic Rockbox kernel primitive, nothing rk27xx- or
USB-specific):
```
60071e28 <enable_interrupt.constprop.0>:
60071e28: mrs  r3, CPSR
60071e2c: bic  r3, r3, #128      ; clear the IRQ-disable (I) bit
60071e30: msr  CPSR_c, r3        ; re-enable IRQs
60071e34: bx   lr                ; <-- captured PC: returning to caller
```
The caller (`check_tmo_expired_inner.constprop.0`, the immediately adjacent function in
memory, and the SAME function repeatedly implicated by every earlier PC sample this
session under the higher-overhead LCD-forensics build) is a loop walking the
timed-wait thread list, toggling `enable_interrupt()`/`disable_interrupt()` around each
list node to keep the IRQ-disabled window short — a completely ordinary, unremarkable
kernel primitive. The very next instruction after our captured return is:
```
60071e74: ldr r7, [r4, #64]     ; a plain memory read, immediately following the capture
```

**Interpretation.** `bx lr` is a single-cycle branch — it cannot itself hang. Since this
is the *last* PC the tick-driven capture could record (by definition: the tick stops
after this point, so there is no later sample), the true freeze must be at or shortly
after this point. The prime candidate is the very next instruction: a plain `ldr` from
`r4+64`. On this CPU, if a memory/bus access is issued to an address the bus never
acknowledges, the pipeline stalls indefinitely — no data abort is raised (an abort
requires the bus to respond with an error; a bus that simply never responds at all
produces a silent, permanent stall instead), and no further instructions — including
interrupt servicing — can execute. This exactly matches every fact gathered this
session:
- **`F=0` always** — a bus stall is not an abort condition, so the fault-visible path
  was never going to fire, no matter how many builds tried to catch it there.
- **`T=D` freezing together** — any further CPU activity, including the tick itself,
  requires bus access, which is now permanently blocked.
- **Every PC sample this session (both this measurement and the earlier, higher-overhead
  LCD-forensics builds) lands in generic scheduler/kernel code, never USB-specific
  code** — because the bus wedge can strike *any* code the instant it next touches the
  bus; the scheduler is simply the most frequently-running code, so it is statistically
  the most likely thing to be "caught in the act."
- **The host sees total silence, including no response to a hardware bus reset**
  (section 5.1/5.2) — if the AHB bus itself is wedged, the CPU cannot service the UDC
  registers needed to react to a reset either, regardless of what the wire does
  electrically.

**Leading hypothesis, now measured rather than inferred:** the AHB bus gets wedged by a
peripheral's DMA engine left mid-transfer — most likely the UDC's own bulk-OUT DMA,
given the write-specific trigger — and the *very next* unrelated memory access by *any*
code (not a bug in that code) hangs the CPU forever. This directly confirms hypothesis
#2 from the (now former) section 7 list below, previously only a guess.

~~**Still open:** exactly which UDC (or other peripheral) register sequence leaves the
AHB master held, and why.~~ Answered in s7.1 below.

---

## 7.1 THE FIX — stock RX discipline in ep_read() (`C4PRO_USB_RX_DMA_GUARD`), verified

**Ground truth extracted from the stock firmware** (2026-07-02): `SYSTEM_BASE.RKW`
(`out/c4_stock_extract/`, plain unscrambled ARM, loads at `0x60000000`, body at file
offset `0x2c`) was disassembled around its UDC driver. Key stock routines, verified by
matching its known EP config (`RX1CON=0x190`) and the `"USBC"` CBW-signature literal:

- `@0x6002ba68` — bulk-OUT DMA arm: `DMAOUTLMADDR = buf; DMACTLO = DMA_START` —
  **and nothing else. Stock never writes RXSTAT on the receive path** (it reads it in
  the RXACK handler for the packet byte count + error bits `0x02030000`). The upstream
  Rockbox `RXSTAT(endp) = xfer_size` write mirrors the TX side (where `TXSTAT = len`
  is meaningful) but has no stock counterpart; its effect on the RX FIFO/DMA state
  machine is undocumented.
- `@0x6002b52c` — RXACK handler: in the write data phase, the DMA is armed **only when
  a packet is already sitting in the FIFO** (reactive drain), so the engine idles
  disarmed between packets and every arm has a full USB packet-time of margin after
  the previous drain. Upstream Rockbox instead re-arms immediately in the same IRQ
  with **no check that the engine is idle** — under AHB contention (NAND flush, LCD
  DMA, interleaved reads: all the empirically-known freeze aggravators) the previous
  drain can still be in flight, and `DMA_START` gets fired into a live engine.
- `@0x6002b9ec` — stock bulk-IN send: waits for its own DMA to self-clear before
  returning — confirming the DMA-idle discipline this driver had already copied for
  TX (`ep_write`), but never for RX.

**The fix** (`ep_read()`, `usb-drv-rk27xx.c`, flag `C4PRO_USB_RX_DMA_GUARD`,
default-on in `colorflyc4pro.h`):
1. no `RXSTAT` write on the receive path (mirror stock);
2. bounded iteration-capped wait for `DMAOUTCTL & DMA_START` to self-clear before
   arming (mirror of the TX-side guard);
3. if the wait expires (engine genuinely stuck): `DMAOUTCTL = 0` + `RXCLR` pulse —
   recover the engine **at arm time** — then arm cleanly. Discarding
   ACKed-but-undrained FIFO bytes here can only make the transfer never complete
   (no CSW success is ever sent), degrading to the host's ordinary 30 s
   command-retry path — never to silent data corruption.
Diag counters `B` (arm found engine busy) / `S` (stuck→recovered) are painted on the
heartbeat screen.

**Verification (wire-measured, then field gate):**
- v1 (steps 1+2 only): a full session captured on usbmon showed the transformation
  directly — 13,392 write commands with exactly ONE 30 s data-phase stall, after which
  the device **stayed alive** (answered GET DESCRIPTOR in 160 ms), the host retried
  the same WRITE(10) and it succeeded. Before the fix the identical signature ended in
  a dead CPU and a failed re-enumeration every time.
- v2 (adding step 3): B=3 S=3 observed (stuck engines recovered at arm time), zero
  stalls, zero cancels, zero Check Conditions on the wire for the whole session.
- Final gate on a freshly quick-formatted volume: **900+ MB copied with zero freezes**
  (at ~1.5 MB/s — throughput work is a separate, pre-existing track).

**Confound worth recording:** several late-day "copy hangs" were NOT the device at
all — a single damaged directory entry (`FAT-fs: fat_bmap_cluster: request beyond EOF
(i_pos 498184)`, host dmesg) left by an earlier torn copy made the host vfat driver
remount the volume read-only on contact, killing copies host-side while the device
answered everything perfectly (wire-verified). Debris from freeze-torn copies
masqueraded as new freezes. **Lesson: after any torn-write incident, reformat (or at
least fsck) the volume before using further copy behavior as evidence.**

---

## 8. Remaining possible causes (hypotheses, pre-s7 — kept for history)

Ordered by estimate of likelihood at the time. Superseded by the measurement in s7;
kept verbatim as a record of what was suspected before direct data existed.

1. ~~**Reset/completion race re-arming a DMA to a torn-down buffer.**~~ **FALSIFIED by
   the 2026-07-02 usbmon capture (section 5.1):** no bus reset precedes the wedge — the
   resets happen 30 s after the device is already dead. Attempts #10/#11/#12 harden a
   path that never runs before the freeze.
2. ~~**A wedged UDC or NAND DMA engine holding the AHB bus**, so a *subsequent* bus
   access in any subsystem busy-spins.~~ **CONFIRMED by the s7 measurement** — this is
   now the leading, measured explanation, not a guess. DMA-abort (#11) helped but did
   not fully fix, because it only runs on a reset arriving *after* the wedge, not at the
   point the wedge occurs.
3. **An unlocated `disable_irq` critical section wrapping an unbounded hardware poll.**
   Superseded by s7 — the captured PC is not inside any `disable_irq` section (it is the
   `bx lr` returning FROM one), and the mechanism (a bus stall, not a software spin)
   does not require one.
4. **A genuine hardware bus deadlock** left by the link reset that only a full UDC
   controller reset would clear — which is deliberately avoided on the C4 (it tears down
   the ROM-established HS PHY state). If so, the correct fix is a controlled full
   re-enumeration, not a spin cap.
5. **The re-enumeration path after a reset** (SETUP handling / descriptor sends via the
   driver) touching state the concurrent reset is tearing down.

---

## 9. Kept improvements (independent of the freeze)
Hardware-ECC reads (~4.4 MB/s), full 30 GB volume, multi-die fast format, TMR0CON
tick-drain, DMA-abort-on-reset, pipelined flush (N=3), and the EQ / LEFT+RIGHT keylock
keymap. All are flag-gated and default-on for the C4; none is implicated in the freeze
and all are worth keeping.

---

## 10. Diagnostic flags reference (for whoever resumes this)
Runtime build defines used during this investigation (see `firmware/export/config/colorflyc4pro.h`
for defaults and opt-outs):

- `C4PRO_BOOT_HEARTBEAT` — paints the T/U/W/D/P/F/R/X/C/IRQ counters on the MSC screen +
  forces backlight on. **This is the on-device instrument; keep it on when diagnosing.**
- `C4PRO_FAULT_VISIBLE` — UIE paints `*** CPU FAULT *** PC=…` on a data/prefetch abort
  (has never fired here — F=0).
- `C4PRO_USB_TIMER_PRIO` — TMR0CON tick-drain (attempt #4).
- `C4PRO_USB_RESET_DMA_ABORT` — DMA-abort on USBRST (attempt #11). Opt out:
  `C4PRO_USB_NO_RESET_DMA_ABORT`.
- `C4PRO_USB_RESET_DEFER` / `C4PRO_USB_RESET_STALE_GUARD` — attempt #12 (untested). Opt
  out: `C4PRO_USB_NO_RESET_DEFER` / `C4PRO_USB_NO_RESET_STALE_GUARD`.
- `C4PRO_FTL_EAGER_FLUSH` + `C4PRO_FTL_FLUSH_KEEP=3` — pipelined flush (attempt #8). Opt
  out: `C4PRO_FTL_NO_EAGER_FLUSH`.
- `C4PRO_CACHE_SPIN_CAP` — CACHEOP cap (attempt #9, never fires).
- `C4PRO_USB_STANDARD_TRANSFER` — bypass polled sender (attempt #10, **leave OFF** — it
  added boot flakiness and did not help).
- `C4PRO_USB_RX_DMA_GUARD` — **THE FIX (s7.1), default-on** in colorflyc4pro.h. Stock
  RX discipline in `ep_read()`: no RXSTAT write, DMA-idle wait before arm,
  stuck-engine recovery (abort+RXCLR) at arm time. Diag counters painted as B/S on the
  heartbeat screen. Opt out: `C4PRO_USB_NO_RX_DMA_GUARD`.
- `C4PRO_WDT_CRASH_CAPTURE` — **the measurement mechanism behind s7.** Arms the
  hardware watchdog, pets it every tick while stashing the current interrupted PC into
  a DRAM record that survives the reset, and reports it on the next boot. JTAG-free
  root-cause measurement; see s7 for the full design and the field-corrected `WDTLR`
  timing note. Kept (small, flag-gated, proven) for future freeze-class bugs.
- Removed after serving their purpose (2026-07-02, post-fix cleanup): the
  `C4PRO_WEDGE_MARKS` LCD forensics (per-USB-packet phase-color painting — which
  itself added IRQ-masked LCD overhead in the hot path under test and accelerated the
  failure; its tick-driven PC readout was superseded by the WDT crash capture),
  `C4PRO_USB_RECV_WATCHDOG` (tick-driven receive re-arm — could not help a true bus
  stall since the tick dies too; superseded by the RX guard's at-arm recovery), and
  `C4PRO_WDT_SURVIVAL_TEST` (one-shot DRAM-survival proof, absorbed into the crash
  capture). Recover any of them from git history if ever needed.

Each opt-out flag lets the next investigator A/B any single change to attribute
causality. Build command template (native installer):

```
PATH=/home/groove/.local/armshim:$PATH \
  C4_NATIVE_OWNED_PBLK_COUNT=63000 C4_NATIVE_INSTALLER_OWNED_PBLK_COUNT=63000 \
  C4_RB_EXTRA_DEFINES="-DC4PRO_NAND_HWECC -DC4PRO_NAND_FAST_FORMAT" \
  C4_NATIVE_RUNTIME_EXTRA_DEFINES="-DC4PRO_NAND_HWECC -DC4PRO_NAND_FAST_FORMAT \
    -DC4PRO_USB_TIMER_PRIO -DC4PRO_BOOT_HEARTBEAT -DC4PRO_FAULT_VISIBLE" \
  scripts/c4_build_native_rockbox_installer.sh
```
