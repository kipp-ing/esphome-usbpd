# How we built it

This firmware was written in one day (2026-09-30) by Claude Code acting as an orchestrator over four
role agents. About 65 minutes of agent work happened without the board and about 25 minutes with it.
The detailed timeline is in [`report-2026-09-30.md`](report-2026-09-30.md). This page explains the
method and why it replaced the earlier approach.

## Before: the Pi chain

Before this day, the same goal was worked on for several days by a local model ("Pi", a Qwen model)
driven round by round by a harness script in a private mission-control repository. It made **188
attempts** and never decoded a single PD message. What that taught us:

- **Guessing does not converge.** The operator's words: *"mit raten bekommen wir es nicht hin"*
  (we won't get there by guessing). Fixes were tried against the board without a written contract of
  how the chip and protocol must behave. This led to "facts first, then a spec".
- **Small sessions, one deliverable each.** Long sessions ended in deadline kills with nothing
  committed. The rule became: one session = one deliverable = one commit.
- **A retro after every round.** Each round wrote a short retrospective, which made repeated mistakes
  visible.
- **Simulate the state machine first.** While the hardware was locked, the logic could still be
  proven on the host. This became the host sim.
- **Keep reasoning effort moderate.** More "thinking" per step did not help the local model.

## The operator's assignment

Original prompt (German, 08:34 UTC):

> Hi, also das mit dem pi, das war sehr zäh. und naja. daher komplett neuer versuch. bitte mach ein
> neues lokales repo hier. wir bauen das mit dir mal selbst. zum vergelich. das Kabel muss dran
> bleiben, das ladegerät kannst du ja schalten. nutze mehre agenten: mr knowitall (kennt oder holt
> spec für chip und pd, kann die schongeholten daten nutzen) . mr spec.: Schreibt wie es funktionieren
> soll. mr engineer. schreibt programme (vorgabe : STatemachine) und mr bench. nur er darf flashen,
> resetten, ladegrät schalten und sorgt dafür dass die MEssdaten automatisiert, filterbar bei mr.
> engineer landen.  ok?

In English: the Pi approach was slow, so start fresh in a new repo, built by Claude directly, for
comparison. The cable stays connected; the charger can be switched. Use several agents: mr-knowitall
(knows or fetches the chip and PD specs), mr-spec (writes how it should work), mr-engineer (writes the
programs, as a state machine) and mr-bench (the only one who flashes, resets and switches the charger,
and who makes sure measurements reach the engineer automatically and in filterable form).

## The roles

| Role | Owns | May not |
|---|---|---|
| Orchestrator (main session) | the plan, briefs, handovers between roles | write the artifacts of a role itself |
| mr-knowitall | `docs/knowledge/`: facts, each with a source reference | write firmware, touch the rig |
| mr-spec | `docs/spec.md`, resolutions in `docs/spec-questions.md` | write firmware, touch the rig |
| mr-engineer | `firmware/fw/`, `firmware/test/host/` | flash, reset, open the serial port, switch the charger |
| mr-bench | `firmware/bench/`, `firmware/runs/` | change firmware logic |

The rules for all roles are in [`../AGENTS.md`](../AGENTS.md).

## The order of work

1. **Rig map.** A read-only explore agent collected the rig facts (board, pins, toolchain, charger
   control, where the spec texts are, what success means).
2. **Parallel start.** mr-knowitall (knowledge base), mr-bench (tooling, self-tested against a fake
   board) and mr-engineer (infrastructure only: logger, I2C layer, console, state-machine skeleton,
   host test harness) started at the same time. The engineer was told explicitly *not* to write PD
   logic yet.
3. **Spec.** Once the knowledge base existed, mr-spec wrote `docs/spec.md` to fit the skeleton the
   engineer had built: 16 states, 85 transition rows, message codec with byte-exact test vectors, a
   JSONL log contract, 38 host test cases and bench acceptance criteria per milestone.
4. **Implementation against the spec.** The engineer implemented the full spec and the host tests.
   Where the spec was unclear, the engineer wrote a question into `docs/spec-questions.md` and used the
   spec's stated default. mr-spec answered each question with a "Resolution" line and updated the
   spec; the engineer then implemented the change. Host sim: 47/47 green.
5. **Bench.** In the evening the board was attached. mr-bench worked through a fixed checklist
   (M0 rig, M4 first Source_Capabilities, M5 5 V contract twice, M6 requested voltage).

## Rules that made it work

- **Facts → spec → code → host sim → board.** No step started before the one it depends on existed.
- **Every fact has a citation.** Datasheet line, spec section or table, or a pinned commit of the
  Linux driver. See [`sources.md`](sources.md).
- **Questions are written down, not guessed.** `docs/spec-questions.md` is the channel between the
  engineer and the spec author.
- **Only one role touches hardware.** The engineer sees board data only as JSONL through `bench/q`,
  never as raw serial output.
- **Stop rule.** If a milestone fails, mr-bench makes at most about 3 runs, then stops with a failure
  report: run directory, decoded evidence and the exact `bench/q` queries for the engineer. This is
  how the one real hardware bug (a stack overflow from log buffers on the caller's stack) went from
  bench to engineer and back in 7 minutes.
- **Commit after every step.** Every role commits its own files in small pieces.
- **Instrument the fix.** The stack fix also added `hb.stack_free`, so the bench could measure the
  headroom instead of assuming it.

## The prompts

- Morning (phase 1): [`prompts-2026-09-30-morning.md`](prompts-2026-09-30-morning.md)
- Evening (phase 2): at the end of [`report-2026-09-30.md`](report-2026-09-30.md)
