.pragma library

// Lookups over the session's stack lists ({id, name, icon, ...} arrays).

function findById(stacks, id) {
    if (!stacks || !id)
        return null
    for (let i = 0; i < stacks.length; ++i) {
        if (stacks[i].id === id)
            return stacks[i]
    }
    return null
}

function displayName(stack) {
    return stack ? (stack.name || stack.id) : ""
}
