---
tags:
  - command
---

# /autodrink

## Syntax

<!--cmd-syntax-start-->
```eqcommand
/autodrink [#] [option]
```
<!--cmd-syntax-end-->

## Description

<!--cmd-desc-start-->
Adds and modifies settings, turns on and off autodrink.
<!--cmd-desc-end-->

## Options

| Option | Description |
|--------|-------------|
| `(no option)` | force manual drinking |
| `1|0` | Turns on and off autodrink. 1=on, 0=off. |
| `*####*` | sets level where plugin should auto drink. example: `/autodrink 3500` |
| `add` | adds drink from your cursor to the .ini list |
| `reload` | reload the .ini |
| `list` | list of drinks in the .ini |
| `warn` | toggles drink warning on or off |
| `announceConsume` | toggles consumption notification on or off |