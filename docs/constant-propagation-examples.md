# Constant Propagation and Folding: Code Transformation Examples

This document provides detailed examples showing how the Constant Propagation and Folding optimization transforms code in the MITScript VM. Each example shows the source code, the unoptimized IR/bytecode, and the optimized output.

## Table of Contents
1. [Basic Arithmetic Folding](#1-basic-arithmetic-folding)
2. [Chained Arithmetic Computations](#2-chained-arithmetic-computations)
3. [Comparison Folding](#3-comparison-folding)
4. [Boolean Logic Folding](#4-boolean-logic-folding)
5. [Unary Operation Folding](#5-unary-operation-folding)
6. [Branch Elimination](#6-branch-elimination)
7. [Dead Branch with Complex Condition](#7-dead-branch-with-complex-condition)
8. [Mixed Constants and Variables](#8-mixed-constants-and-variables)

---

## 1. Basic Arithmetic Folding

### Source Code (MITScript)
```
x = 5 + 3;
print(x);
```

### Unoptimized IR (CFG)
```
Block 0:
  v0 = LoadConst CONSTI(5)      // Load constant 5
  v1 = LoadConst CONSTI(3)      // Load constant 3
  v2 = Add v0, v1               // v2 = 5 + 3 (computed at runtime)
  StoreLocal "x", v2            // Store result in x
  v3 = LoadGlobal "print"       // Load print function
  v4 = Call v3, v2              // Call print(x)
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0:
  v0 = LoadConst CONSTI(5)      // (may be removed by DCE)
  v1 = LoadConst CONSTI(3)      // (may be removed by DCE)
  v2 = LoadConst CONSTI(8)      // FOLDED: 5 + 3 = 8
  StoreLocal "x", v2
  v3 = LoadGlobal "print"
  v4 = Call v3, v2
  Return
```

### Unoptimized Bytecode
```
0:  LoadConst 0        // Push 5 (constants[0] = 5)
1:  LoadConst 1        // Push 3 (constants[1] = 3)
2:  Add                // Pop 3, 5; Push 8
3:  StoreLocal 0       // Store to x
4:  LoadGlobal 0       // Load "print"
5:  LoadLocal 0        // Load x
6:  Call 1             // Call with 1 argument
7:  Pop                // Discard return value
8:  LoadConst 2        // Push None (constants[2] = None)
9:  Return
```

### Optimized Bytecode
```
0:  LoadConst 0        // Push 8 (constants[0] = 8, folded!)
1:  StoreLocal 0       // Store to x
2:  LoadGlobal 0       // Load "print"
3:  LoadLocal 0        // Load x
4:  Call 1             // Call with 1 argument
5:  Pop
6:  LoadConst 1        // Push None
7:  Return
```

**Savings:** 2 fewer instructions, 1 fewer constant pool entry

---

## 2. Chained Arithmetic Computations

### Source Code (MITScript)
```
a = 10;
b = 20;
c = a + b;
d = c * 2;
print(d);
```

### Unoptimized IR (CFG)
```
Block 0:
  v0 = LoadConst CONSTI(10)
  StoreLocal "a", v0
  v1 = LoadConst CONSTI(20)
  StoreLocal "b", v1
  v2 = LoadLocal "a"
  v3 = LoadLocal "b"
  v4 = Add v2, v3               // c = a + b (runtime)
  StoreLocal "c", v4
  v5 = LoadLocal "c"
  v6 = LoadConst CONSTI(2)
  v7 = Mul v5, v6               // d = c * 2 (runtime)
  StoreLocal "d", v7
  v8 = LoadGlobal "print"
  v9 = LoadLocal "d"
  v10 = Call v8, v9
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0:
  v0 = LoadConst CONSTI(10)
  StoreLocal "a", v0
  v1 = LoadConst CONSTI(20)
  StoreLocal "b", v1
  v2 = LoadConst CONSTI(10)     // Propagated from a
  v3 = LoadConst CONSTI(20)     // Propagated from b
  v4 = LoadConst CONSTI(30)     // FOLDED: 10 + 20 = 30
  StoreLocal "c", v4
  v5 = LoadConst CONSTI(30)     // Propagated from c
  v6 = LoadConst CONSTI(2)
  v7 = LoadConst CONSTI(60)     // FOLDED: 30 * 2 = 60
  StoreLocal "d", v7
  v8 = LoadGlobal "print"
  v9 = LoadConst CONSTI(60)     // Propagated from d
  v10 = Call v8, v9
  Return
```

### Optimized Bytecode (with DCE applied)
```
0:  LoadConst 0        // Push 60 (fully folded result)
1:  StoreLocal 3       // Store to d
2:  LoadGlobal 0       // Load "print"
3:  LoadLocal 3        // Load d
4:  Call 1
5:  Pop
6:  LoadConst 1        // None
7:  Return
```

**Key Insight:** The entire computation chain `(10 + 20) * 2 = 60` is folded at compile time.

---

## 3. Comparison Folding

### Source Code (MITScript)
```
x = 5 > 3;
y = 10 == 10;
z = 7 < 2;
```

### Unoptimized IR (CFG)
```
Block 0:
  v0 = LoadConst CONSTI(5)
  v1 = LoadConst CONSTI(3)
  v2 = CmpGt v0, v1             // 5 > 3 (runtime)
  StoreLocal "x", v2

  v3 = LoadConst CONSTI(10)
  v4 = LoadConst CONSTI(10)
  v5 = CmpEq v3, v4             // 10 == 10 (runtime)
  StoreLocal "y", v5

  v6 = LoadConst CONSTI(7)
  v7 = LoadConst CONSTI(2)
  v8 = CmpLt v6, v7             // 7 < 2 (runtime)
  StoreLocal "z", v8
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0:
  v2 = LoadConst CONSTB(true)   // FOLDED: 5 > 3 = true
  StoreLocal "x", v2

  v5 = LoadConst CONSTB(true)   // FOLDED: 10 == 10 = true
  StoreLocal "y", v5

  v8 = LoadConst CONSTB(false)  // FOLDED: 7 < 2 = false
  StoreLocal "z", v8
  Return
```

### Unoptimized Bytecode
```
0:  LoadConst 0        // 5
1:  LoadConst 1        // 3
2:  Gt                 // 5 > 3
3:  StoreLocal 0       // x = true
4:  LoadConst 2        // 10
5:  LoadConst 2        // 10 (reused)
6:  Eq                 // 10 == 10
7:  StoreLocal 1       // y = true
8:  LoadConst 3        // 7
9:  LoadConst 4        // 2
10: Swap               // For CmpLt: swap operands
11: Gt                 // 2 > 7 (equivalent to 7 < 2)
12: StoreLocal 2       // z = false
```

### Optimized Bytecode
```
0:  LoadConst 0        // true (constants[0] = true)
1:  StoreLocal 0       // x = true
2:  LoadConst 0        // true (reused)
3:  StoreLocal 1       // y = true
4:  LoadConst 1        // false (constants[1] = false)
5:  StoreLocal 2       // z = false
```

**Savings:** 7 fewer instructions, all comparisons eliminated

---

## 4. Boolean Logic Folding

### Source Code (MITScript)
```
a = true & false;
b = true | false;
c = (5 > 3) & (10 == 10);
```

### Unoptimized IR (CFG)
```
Block 0:
  v0 = LoadConst CONSTB(true)
  v1 = LoadConst CONSTB(false)
  v2 = And v0, v1               // true & false (runtime)
  StoreLocal "a", v2

  v3 = LoadConst CONSTB(true)
  v4 = LoadConst CONSTB(false)
  v5 = Or v3, v4                // true | false (runtime)
  StoreLocal "b", v5

  v6 = LoadConst CONSTI(5)
  v7 = LoadConst CONSTI(3)
  v8 = CmpGt v6, v7             // 5 > 3 (runtime)
  v9 = LoadConst CONSTI(10)
  v10 = LoadConst CONSTI(10)
  v11 = CmpEq v9, v10           // 10 == 10 (runtime)
  v12 = And v8, v11             // (5 > 3) & (10 == 10) (runtime)
  StoreLocal "c", v12
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0:
  v2 = LoadConst CONSTB(false)  // FOLDED: true & false = false
  StoreLocal "a", v2

  v5 = LoadConst CONSTB(true)   // FOLDED: true | false = true
  StoreLocal "b", v5

  v8 = LoadConst CONSTB(true)   // FOLDED: 5 > 3 = true
  v11 = LoadConst CONSTB(true)  // FOLDED: 10 == 10 = true
  v12 = LoadConst CONSTB(true)  // FOLDED: true & true = true
  StoreLocal "c", v12
  Return
```

### Optimized Bytecode
```
0:  LoadConst 0        // false
1:  StoreLocal 0       // a = false
2:  LoadConst 1        // true
3:  StoreLocal 1       // b = true
4:  LoadConst 1        // true (reused)
5:  StoreLocal 2       // c = true
```

---

## 5. Unary Operation Folding

### Source Code (MITScript)
```
x = -5;
y = -(3 + 7);
z = !true;
w = !(5 > 10);
```

### Unoptimized IR (CFG)
```
Block 0:
  v0 = LoadConst CONSTI(5)
  v1 = Neg v0                   // -5 (runtime)
  StoreLocal "x", v1

  v2 = LoadConst CONSTI(3)
  v3 = LoadConst CONSTI(7)
  v4 = Add v2, v3               // 3 + 7 (runtime)
  v5 = Neg v4                   // -(3 + 7) (runtime)
  StoreLocal "y", v5

  v6 = LoadConst CONSTB(true)
  v7 = Not v6                   // !true (runtime)
  StoreLocal "z", v7

  v8 = LoadConst CONSTI(5)
  v9 = LoadConst CONSTI(10)
  v10 = CmpGt v8, v9            // 5 > 10 (runtime)
  v11 = Not v10                 // !(5 > 10) (runtime)
  StoreLocal "w", v11
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0:
  v1 = LoadConst CONSTI(-5)     // FOLDED: -5
  StoreLocal "x", v1

  v4 = LoadConst CONSTI(10)     // FOLDED: 3 + 7 = 10
  v5 = LoadConst CONSTI(-10)    // FOLDED: -10
  StoreLocal "y", v5

  v7 = LoadConst CONSTB(false)  // FOLDED: !true = false
  StoreLocal "z", v7

  v10 = LoadConst CONSTB(false) // FOLDED: 5 > 10 = false
  v11 = LoadConst CONSTB(true)  // FOLDED: !false = true
  StoreLocal "w", v11
  Return
```

### Optimized Bytecode
```
0:  LoadConst 0        // -5
1:  StoreLocal 0       // x = -5
2:  LoadConst 1        // -10
3:  StoreLocal 1       // y = -10
4:  LoadConst 2        // false
5:  StoreLocal 2       // z = false
6:  LoadConst 3        // true
7:  StoreLocal 3       // w = true
```

---

## 6. Branch Elimination

This is one of the most powerful optimizations - eliminating entire branches when the condition is known at compile time.

### Source Code (MITScript)
```
if (true) {
    x = 1;
} else {
    x = 2;
}
print(x);
```

### Unoptimized IR (CFG)
```
Block 0 (entry):
  v0 = LoadConst CONSTB(true)
  CondJump v0, trueTarget=1, falseTarget=2

Block 1 (then branch):
  v1 = LoadConst CONSTI(1)
  StoreLocal "x", v1
  Jump target=3

Block 2 (else branch):
  v2 = LoadConst CONSTI(2)
  StoreLocal "x", v2
  Jump target=3

Block 3 (merge):
  v3 = LoadGlobal "print"
  v4 = LoadLocal "x"
  v5 = Call v3, v4
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0 (entry):
  v0 = LoadConst CONSTB(true)
  Jump target=1                 // REWRITTEN: CondJump → Jump (condition is constant true)

Block 1 (then branch):
  v1 = LoadConst CONSTI(1)
  StoreLocal "x", v1
  Jump target=3

Block 2 (else branch - now unreachable):
  v2 = LoadConst CONSTI(2)
  StoreLocal "x", v2
  Jump target=3

Block 3 (merge):
  v3 = LoadGlobal "print"
  v4 = LoadLocal "x"
  v5 = Call v3, v4
  Return
```

### Unoptimized Bytecode
```
0:  LoadConst 0        // true
1:  Not                // Invert for If semantics
2:  If 4               // If false, jump +4 (to else)
3:  LoadConst 1        // 1
4:  StoreLocal 0       // x = 1
5:  Goto 3             // Jump to merge
6:  LoadConst 2        // 2 (else branch)
7:  StoreLocal 0       // x = 2
8:  LoadGlobal 0       // "print"
9:  LoadLocal 0        // x
10: Call 1
11: Pop
12: LoadConst 3        // None
13: Return
```

### Optimized Bytecode (branch eliminated)
```
0:  LoadConst 0        // 1
1:  StoreLocal 0       // x = 1
2:  LoadGlobal 0       // "print"
3:  LoadLocal 0        // x
4:  Call 1
5:  Pop
6:  LoadConst 1        // None
7:  Return
```

**Savings:** 6 fewer instructions, entire else branch eliminated

---

## 7. Dead Branch with Complex Condition

### Source Code (MITScript)
```
if (5 > 10) {
    x = 100;
    y = x * 2;
    print(y);
} else {
    z = 42;
}
```

### Unoptimized IR (CFG)
```
Block 0 (entry):
  v0 = LoadConst CONSTI(5)
  v1 = LoadConst CONSTI(10)
  v2 = CmpGt v0, v1             // 5 > 10
  CondJump v2, trueTarget=1, falseTarget=2

Block 1 (then branch - dead):
  v3 = LoadConst CONSTI(100)
  StoreLocal "x", v3
  v4 = LoadLocal "x"
  v5 = LoadConst CONSTI(2)
  v6 = Mul v4, v5
  StoreLocal "y", v6
  v7 = LoadGlobal "print"
  v8 = LoadLocal "y"
  v9 = Call v7, v8
  Jump target=3

Block 2 (else branch):
  v10 = LoadConst CONSTI(42)
  StoreLocal "z", v10
  Jump target=3

Block 3 (exit):
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0 (entry):
  v2 = LoadConst CONSTB(false)  // FOLDED: 5 > 10 = false
  Jump target=2                 // REWRITTEN: always take else branch

Block 1 (then branch - unreachable):
  ... (entire block is dead code)

Block 2 (else branch):
  v10 = LoadConst CONSTI(42)
  StoreLocal "z", v10
  Jump target=3

Block 3 (exit):
  Return
```

### Optimized Bytecode
```
0:  LoadConst 0        // 42
1:  StoreLocal 0       // z = 42
2:  LoadConst 1        // None
3:  Return
```

**Result:** Entire then branch (9 instructions in IR) is eliminated as dead code.

---

## 8. Mixed Constants and Variables

When computations involve both constants and runtime values, constant propagation still optimizes the constant portions.

### Source Code (MITScript)
```
x = input();
y = 5 + 3;
z = x + y;
print(z);
```

### Unoptimized IR (CFG)
```
Block 0:
  v0 = LoadGlobal "input"
  v1 = Call v0                  // x = input() - runtime value
  StoreLocal "x", v1

  v2 = LoadConst CONSTI(5)
  v3 = LoadConst CONSTI(3)
  v4 = Add v2, v3               // y = 5 + 3
  StoreLocal "y", v4

  v5 = LoadLocal "x"
  v6 = LoadLocal "y"
  v7 = Add v5, v6               // z = x + y (runtime - x is unknown)
  StoreLocal "z", v7

  v8 = LoadGlobal "print"
  v9 = LoadLocal "z"
  v10 = Call v8, v9
  Return
```

### Optimized IR (After Constant Propagation + Folding)
```
Block 0:
  v0 = LoadGlobal "input"
  v1 = Call v0                  // x = input() - still runtime
  StoreLocal "x", v1

  v4 = LoadConst CONSTI(8)      // FOLDED: 5 + 3 = 8
  StoreLocal "y", v4

  v5 = LoadLocal "x"            // Still need to load x (runtime value)
  v6 = LoadConst CONSTI(8)      // y is known to be 8
  v7 = Add v5, v6               // z = x + 8 (NOT folded - x is unknown)
  StoreLocal "z", v7

  v8 = LoadGlobal "print"
  v9 = LoadLocal "z"
  v10 = Call v8, v9
  Return
```

### Key Point
The `Add v5, v6` instruction is **not** folded because `v5` (from `input()`) has value `Top` (unknown) in the lattice. The lattice correctly represents:
- `v4` = `ConstInt(8)` → known constant
- `v5` = `Top` → unknown (from `input()`)
- `v7` = `eval_binary(Add, Top, ConstInt(8))` = `Top` → cannot fold

---

## Lattice Values and Meet Operation

The constant propagation uses a lattice with the following values:

```
          Top (unknown/varying)
         / | \
        /  |  \
       /   |   \
  ConstInt ConstBool ConstString ConstNone
       \   |   /
        \  |  /
         \ | /
         Bottom (unreachable)
```

### Meet Operation (⊓)
```cpp
meet(a, b):
  if a == Bottom: return b
  if b == Bottom: return a
  if a == Top or b == Top: return Top
  if a == b: return a     // Same constant
  return Top              // Different constants → unknown
```

### Example: Loop with Phi-like Merge
```
// Source
x = 0;
while (cond) {
    x = x + 1;
}
```

At the loop header, `x` comes from two paths:
- Initial: `x = ConstInt(0)`
- Back edge: `x = Top` (result of `x + 1` where `x` is unknown in loop)

```
meet(ConstInt(0), Top) = Top
```

Therefore `x` inside the loop is `Top` and `x + 1` cannot be folded.

---

## Summary of Optimizations

| Optimization | Before | After | Savings |
|--------------|--------|-------|---------|
| Arithmetic folding | `Add v0, v1` where both are const | `LoadConst result` | 1 instruction |
| Comparison folding | `CmpGt v0, v1` where both are const | `LoadConst bool` | 1 instruction |
| Boolean logic | `And v0, v1` where both are const | `LoadConst bool` | 1 instruction |
| Unary folding | `Neg v0` where v0 is const | `LoadConst -val` | 1 instruction |
| Branch elimination | `CondJump const_true` | `Jump true_target` | Entire dead branch |

## Implementation Reference

The constant propagation and folding implementation can be found in:
- `src/mitscript-compiler/constant-propagation.hpp:352` - `run_constant_folding()` function
- `src/mitscript-compiler/constant-propagation.hpp:120` - `ConstantPropagation` class
- `src/mitscript-compiler/constant-propagation.hpp:82` - `eval_binary()` for folding binary ops
- `src/mitscript-compiler/constant-propagation.hpp:61` - `eval_unary()` for folding unary ops
