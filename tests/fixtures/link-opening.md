# Links a document may open

A click on a link opens it. A web address or a document opens at once. A
program, a script, a file on another server and any other program's own
address ask first, with the target in full.

| Link | A click |
|:-----|:--------|
| [The web](https://example.com/tinta) | opens in the browser |
| [A report](report.pdf) | opens in its own application |
| [The pictures](pics) | opens the folder |
| [A script beside this file](run.cmd) | asks: Open this link? |
| [A name alone](notepad.exe) | says the file is not found: it is looked for beside this file only |
| [A program](C:/Windows/System32/calc.exe) | asks |
| [Windows settings](ms-settings:display) | asks |
| [A file on a server](//tinta-test-server.invalid/share/tool.exe) | asks, and is not looked for before |

- In the list too: [the report again](report.pdf), and **bold** beside it.
- Esc or Cancel in the dialog opens nothing.

> A quote with [the web](https://example.com/tinta) in it.

```text
Links in code stay text: run.cmd
```
