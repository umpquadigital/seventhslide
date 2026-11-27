import QtQuick

// A window, page or dialog made the first time it is asked for, then kept,
// so what is never opened costs nothing at start. ``load()`` makes it and
// returns it; ``item`` is null until then. ``objectName`` is the type it makes,
// which the tests look it up by.
Loader {
    id: lazy

    active: false

    function load(): QtObject {
        lazy.active = true
        return lazy.item
    }
}
