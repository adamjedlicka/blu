# blu
## blest language. undisputed

blu is small, fast and intuitive scripting language

```
class Dog {
    fn __init(name) {
        @name = name
    }

    fn bark(): @name + " barks!"
}

var maxipes = Dog("Fík")

3.times(fn(i): maxipes.bark()).each(fn(val): System.println(val))
```
