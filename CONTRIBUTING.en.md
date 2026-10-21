

\---



\## 📄 `CONTRIBUTING.en.md` (anglais)



```markdown

\# Contributing to KernelDB



Thank you for your interest in \*\*KernelDB\*\*! This guide explains how to contribute

to the project, whether through a bug report, a suggestion, a documentation

improvement, or code.



KernelDB is an embedded relational engine in C++17, with no external dependencies.

The project is under active development; it is not yet production-ready.

See \[README.md](README.md), \[VISION.md](VISION.md) and \[PROJET.md](PROJET.md)

for context.



\## Code of Conduct



All participation is governed by the \[Code of Conduct](CODE\_OF\_CONDUCT.en.md).

By participating, you agree to uphold it. Please report any unacceptable

behavior to `conduct@kerneldb.dev`.



\## How to contribute



\### 1. Report a bug



Before opening an issue, check that it does not already exist in

\[the issues](https://github.com/kamelbouz/KernelDB/issues).



If the bug is new, open an issue with:



\- \*\*Clear title\*\*: "Crash on INSERT with NULL VARCHAR"

\- \*\*Description\*\*: what you expected vs what happens

\- \*\*Reproducible\*\*: exact SQL commands, or minimal C++ code

\- \*\*Environment\*\*: OS, compiler, KernelDB version (`git rev-parse HEAD`)

\- \*\*Logs\*\*: full output, including error messages



A bug \*\*without a reproducer\*\* cannot be fixed. Take the time to reduce your

case to a minimum.



\### 2. Propose a feature



First open a \*\*discussion\*\* (not an issue) to explain:



\- \*\*The need\*\*: what problem it solves

\- \*\*The proposal\*\*: how you see it

\- \*\*Alternatives\*\*: what you have considered



Wait for feedback before coding. A feature that does not fit the project's

vision will not be accepted, even if well-coded.



\### 3. Improve documentation



Docs are in `README.md`, `docs/`, and `.md` files at the root.

A doc PR is \*\*always welcome\*\* — it's the best first step.



\### 4. Contribute code



\#### Fork and clone



```bash

\# On GitHub: click "Fork"

git clone https://github.com/<your-account>/KernelDB.git

cd KernelDB

git remote add upstream https://github.com/kamelbouz/KernelDB.git

