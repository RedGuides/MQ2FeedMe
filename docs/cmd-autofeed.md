---
tags:
  - command
---

# /autofeed

## Syntax

<!--cmd-syntax-start-->
```eqcommand
/autofeed [#] [option]
```
<!--cmd-syntax-end-->

## Description

<!--cmd-desc-start-->
Adds and modifies settings, turns on and off autofeed.
<!--cmd-desc-end-->

## Options

| Option | Description |
|--------|-------------|
| `(no option)` | force manual feeding |
| `1|0` | Turns on and off autofeed. 1=on, 0=off. |
| `*####*` | sets level where plugin should auto feed. example: `/autofeed 3500` |
| `add` | adds food from your cursor to the .ini list |
| `reload` | reload the .ini |
| `list` | list of foods in the .ini |
| `warn` | toggles food warning on or off |
| `announceConsume` | toggles consumption notification on or off |
