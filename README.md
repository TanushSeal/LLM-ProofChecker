# Proof Checker (Church’s P2 System)

This project provides a simple proof checker for propositional logic (based on **Church’s P2 system**).  
The code is written in C and can be compiled into a shared library for easy use with other programs (e.g. Python, OCaml, or via FFI).

---

## Build Instructions

NOTE: You should have C and Python installed in your system. If you already have them installed check that it is the latest version.

1. Create a working directory:

   ```bash
   mkdir p2
   cd p2

2. Save all the files from this repository into the directory.
3. Compile and build the shared library:

- Step 1: Compile to position-independent object code
 ```bash
gcc -fPIC -c proof_checker.c -o proof_checker.o
```
- Step 2: Link into a shared library

```bash
gcc -shared -o libproofchecker.so proof_checker.o
```
You should see something like:
```bash
0000000000001130 T verify_proof
```
This means all the necessary building has been done.

## Use instructions

Now open the file ai_harness.py , go to line no. 72 and enter your premises, and then in line 73 enter your goal in the indicated places and run the code using:

```bash
python3 ai_harness.py
```


