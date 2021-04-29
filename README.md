# memory poker! (linux only)

Find and change a value in a process' memory!

## Usage

To build, run `make release` (requires a C compiler).
Now, you can mess around with the memory in any process
with `./mem <pid>`. (You can find out the PID of a process
with `pidof <program name>`, or with a task manager.)

Enter the value of a byte in memory multiple times
(so, try changing the value/letting the value change,
and enter it each time).

When you've narrowed it down to a few possibilities
(some things might be stored twice, so you can't necessarily
always reduce it down to 1), enter `x` to switch to poking mode.

For clarification, see the guide below:

## Guide

Let's say there's a value you want to change in a program.
If it's a floating-point number (i.e. a
decimal number rather than an integer)
it's going to be harder (not explained here).

*Warning: There is a decent possibility that the program will crash!*

Repeat this procedure several times:
- Calculate the current value, [modulo 256](https://www.google.com/search?q=1329+mod+256)
- Enter the value into `mem`.
- Wait for the program to resume.
- Try changing the value/letting the value change.

Each time, `mem` will print out the number of possibilities. Once this
number has gotten down to something reasonable (<100), and isn't going
down any further, enter `x` to start messing around.

For the address, you can just press enter to use the default.
Then, enter the value you want, and after a few seconds, the program should update
accordingly. Unfortunately, you can only change the value modulo 256 (so you couldn't change
it to something higher than 255).
This isn't guaranteed to work, and there's a good chance it'll only
half-work, but hey, it might!

## Bugs

This was quickly cobbled together. There might be
bugs, but I probably won't fix them (sorry).

## License

```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
```
