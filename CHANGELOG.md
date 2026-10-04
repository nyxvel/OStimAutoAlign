# Changelog

## 1.2.0

- Aligns the visible shaft, not its bones: the centre line comes from the skinned mesh. UBE's futa
  addon has its Gen bones along the top of the shaft (~2 units off centre), so 1.1 aimed it at the
  lower lip/chin and below the opening.
- Mouth target is the centre of MouthU/MouthD/MouthL/MouthR.
- Waits for sane readings after a scene starts (a transient glitch reads parts ~44 units apart);
  frames more than 3x the median are ignored, and "move nothing" is always a candidate.
- Re-resolves body parts when an actor's 3D is rebuilt.

## 1.1.0

- Prefers moving the actors over bending the shaft: 1.0 bent shafts down to reach a mouth, or far
  off the canal in missionary scenes.
- Direction cost: the shaft should point along the canal, into the mouth, through the fist
  (`angleDeadzone`, `anglePerUnit`).
- Separate costs for lifting (`costHeight`) and sinking (`costSink`); big changes cost more than
  small ones; both actors can move in one fit.
- Alignments recorded by 1.0 are reset to OStim's default and measured again.

## 1.0.0

First release.
