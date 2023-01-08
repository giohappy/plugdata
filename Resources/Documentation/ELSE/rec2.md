---
title: rec2

description: Record note messages into text

categories:
- object

pdcategory:

arguments:
- type: symbol
  description: text name (necessary)
  default:

inlets:
  1st:
  - type: float
    description: MIDI pitch
  - type: bang
    description: reset the recording
  2nd:
  - type: float
    description: MIDI velocity

outlets:
  1st:
  - type:
    description:

draft: false
---

[record] records to a [text] object that is suitable for [text sequence].