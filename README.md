# GemVM Language

---

<p align="center">
  <img src="assests/logo.svg" alt="GemVM Logo" width="120"/>
</p>

<p align="center">
  <b>Gem</b> is a lightweight, scripting language based on a Virtual Machine.<br/>
  <i>Just a hobby :P</i>
</p>

---

## Table of Contents

- [Usage](#usage)
- [Value Types](#value-types)
- [Printing](#printing)
- [Statements and Expressions](#statements-and-expressions)
- [Keywords and Control Flow](#keywords-and-control-flow)
- [Functions](#functions)
- [Classes](#classes)
- [Error Handling](#error-handling)
- [Imports and Modules](#imports-and-modules)
- [Standard Library](#standard-library)
  - [Math Class](#math-class)
  - [Window Class](#window-class)
  - [String Methods](#string-methods)
  - [List Methods](#list-methods)
- [Credits](#credits)

---

## Usage

### Linux

For arch linux:

```
git clone https://github.com/gift-panda/GemVM
cd GemVM
mkdir build && cd build
cmake ..
sudo make install
```

### Windows

Use the precompiled .exe in the `Windows/` directory.

```
.\GemVM.exe filename
```

---

## Value Types

### Numbers
```gem
var a = 42;
var b = 3.14;
```

### Strings
```gem
var s = "hello";
println(s.length()); // 5
println(s.charAt(1)); // "e"
```

### Booleans
```gem
var flag = true;
```

### Nil
```gem
var nothing = nil;
```

### Lists (Arrays) and Indexing
```gem
var l = [1, 2, 3];
println(l[0]); // 1
l[1] = 42;
println(l); // [1, 42, 3]
```

### Objects and Classes
```gem
class Point {
    init(x, y) {
        this.x = x; this.y = y;
    }
}
var p = Point(1, 2);
```

---

## Printing

```gem
print(true);
println("world!");
println(123);
println([1, 2, 3]);
```

---

## Statements and Expressions

### Variable Declaration
```gem
var x = 10;
```

### Assignment
```gem
x = 20;
```

### Expressions
```gem
var y = x + 5 * 2;
```

---

## Keywords

### if / else
```gem
if (x > 10) {
    println("Big!");
} else {
    println("Small!");
}
```

### while Loop
```gem
var i = 0;
while (i < 5) {
    println(i);
    i = i + 1;
}
```

### for Loop
```gem
for (var i = 0; i < 3; i = i + 1;) {
    println(i);
}
```

### break and continue
```gem
for (var i = 0; i < 10; i = i + 1;) {
    if (i == 5) break;
    if (i % 2 == 0) continue;
    println(i);
}
```

### return
```gem
func add(a, b) {
    return a + b;
}
println(add(2, 3)); // 5
```

---

## Functions

### Declaration and Call
```gem
func greet(name) {
    println("Hello, " + name + "!");
}
greet("GemVM");
```

### Closures
```gem
func makeAdder(x) {
    func lambda(y) {
        return x + y;
    }
    return lambda;
}
var add5 = makeAdder(5);
println(add5(10)); // 15
```

### Function Overloading (by arity)
```gem
func foo() {
    println("no args");
}
func foo(x) {
    println("one arg: " + x);
}
foo();
foo(42);
```

---

## Classes

### Basic Class
```gem
class Counter {
    init() {
        this.value = 0;
    }
    inc() {
        this.value = this.value + 1;
    }
}
var c = Counter();
c.inc();
```

### Static and Instance Fields/Methods

- **Static variables**: Declared directly in the class body.
- **Instance variables**: Created by assigning to `this.x` inside any class method (including `init`).

```gem
class Example {
    var staticField = 123; // static variable

    static staticMethod() {
        println("static!");
    }
    instanceMethod() {
        this.instanceField = 42;
        println("instance!");
    }
}
Example.staticMethod();
var e = Example();
e.instanceMethod();
println(e.instanceField); // 42
```

### Private and Public Fields

- **Private fields**: Use `#` prefix, e.g. `var #hidden = 42;` (static) or `this.#hidden = 42;` (instance).
- **Public fields**: No prefix.

```gem
class Secret {
    var #hidden = 42; // static private
    var visible = 1;  // static public
    setHidden(val) {
          Secret.#hidden = val;
    }
    getHidden() {
        return Secret.#hidden;
    }
}
var s = Secret();
println(s.visible);      // 1
// println(s.#hidden);   // Error: Cannot access private field
s.setHidden(99);
println(s.getHidden());  // 99
```

### Inheritance and super
```gem
class Animal {
    speak() {
        println("...");
    }
}
class Dog :: Animal {
    speak() {
        super.speak();
        println("Woof!");
    }
}
var d = Dog();
d.speak();
```

### Operator Overloading

- Use the `operator` keyword in the class body:
```gem
class Vec2 {
    init(x, y) {
        this.x = x; this.y = y;
    }
    operator +(other) {
        return Vec2(this.x + other.x, this.y + other.y);
    }
}
var a = Vec2(1, 2);
var b = Vec2(3, 4);
var c = a + b; // Vec2(4, 6)
```

---

## Error Handling

```gem
try {
    throw Error("Something went wrong!");
} catch (e) {
    println(e.msg);
    println(e.stackTrace);
}
```

Make your own Errors:
```
class CustomError :: Error{

}
throw CustomError("Hey this is an Error!");
```
---

## Imports and Modules

```gem
import Math;
import Window;
import mymodule.MyClass;
```

---

## Standard Library

### Math Class

**Constants:**  
- `Math.PI`, `Math.E`, `Math.TAU`, `Math.DEG2RAD`, `Math.RAD2DEG`

**Methods:**
- `Math.abs(x)`
- `Math.min(a, b)`
- `Math.max(a, b)`
- `Math.clamp(x, a, b)`
- `Math.sign(x)`
- `Math.pow(a, b)`
- `Math.sqrt(x)`
- `Math.cbrt(x)`
- `Math.exp(x)`
- `Math.log(x)`
- `Math.log10(x)`
- `Math.sin(x)`
- `Math.cos(x)`
- `Math.tan(x)`
- `Math.asin(x)`
- `Math.acos(x)`
- `Math.atan(x)`
- `Math.atan2(y, x)`
- `Math.floor(x)`
- `Math.ceil(x)`
- `Math.round(x)`
- `Math.trunc(x)`
- `Math.mod(a, b)`
- `Math.lerp(a, b, t)`

**Example:**
```gem
println(Math.abs(-5));      // 5
println(Math.max(2, 10));   // 10
println(Math.sin(Math.PI)); // 0
```

---

### Window Class

**Methods:**
- `Window.init(width, height, title)`
- `Window.clear(color)`
- `Window.drawRect(x, y, w, h, color)`
- `Window.drawCircle(x, y, radius, color)`
- `Window.drawImage(x, y, imageOrPath[, width, height])`
- `window.drawTrig(x1, y1, x2, y2, x3, y3, color)`
- `window.drawLine(x1, y1, x2, y2, color, [thickness]`
- `Window.loadImage(path[, width, height])`
- `Window.update()`
- `Window.pollEvent()`
- `Window.getMousePos()`
- `Window.exit()`

**Example: Window Creation and Event Handling**
```gem
Window.init(800, 600, "Demo");
while (true) {
    Window.clear(0x222222);
    Window.drawRect(100, 100, 200, 150, 0xFF0000);
    Window.update();

    var event = Window.pollEvent();
    if (event != nil) {
        if (event[0] == "quit") break;
        if (event[0] == "mouse_down") {
            println("Mouse button: " + event[1]);
        }
        if (event[0] == "key_down") {
            println("Key pressed: " + event[1]);
            if (event[1] == "Escape") break;
        }
    }
}
Window.exit();
```

**Mouse and Keyboard Example:**
```gem
var pos = Window.getMousePos();
println("Mouse at: " + pos[0] + ", " + pos[1]);
```

---

### String Methods

```gem
var s = "hello";
println(s.length());         // 5
println(s.charAt(1));       // "e"
println(s.toUpperCase());   // "HELLO"
println(s.substring(1, 4)); // "ell"
println(s.indexOf("l"));    // 2
println("123".asNum());     // 123
println("true".asBool());   // true
println("A".charCode());    // 65
println("42".parse());      // 42
println("a b cd".split(" ")); //["a","b","cd"]
println("abcd".startsWtih(...));
println("abcd".endsWith(...));
```

---

### List Methods

```gem
var l = [1, 2, 3];
l.append(4);
println(l.length());      // 4
println(l.get(2));        // 3
l.set(1, 99);
println(l);               // [1, 99, 3, 4]
l.pop();
l.insert(1, 42);
l.clear();
println(l.contains(42));  // false
```
---

## Credits

- Book Crafting Interpreters by Robert Nystrom.

---


> For more examples, see the `chess/`, `Raycast/` and `3dRenderer/` directories.
