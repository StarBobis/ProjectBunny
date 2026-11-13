# Variables

Variables store values that can be assigned and referenced by INI scripts.

## Variable Declaration

Global variables can be initialized using expressions. Expressions are evaluated when the declaration is parsed.

```ini
[Constants]
global $foo = 1
global $bar = $foo + 1

[CommandListFoo]
local $var = $bar + 1
```

The value of `$foo` is evaluated at parse time, and the resulting value is assigned to `$bar`.

Many numeric and boolean section options accept expressions. Their values are snapshots evaluated during parsing; declarations must be available in the appropriate namespace. This does not make every string-valued option an expression.

Early System and Rendering options are parsed before Constants declarations, so they cannot reference those variables. Unavailable declarations use the option's normal failure/default behavior; reloading does not make the previous configuration's declarations available. Resource and pool options parsed after Constants can use the current declarations.

## Locked Variables

The `locked` keyword prevents a variable from being used as an assignment target.

For example:

```ini
global locked $var = 1.23
```

The variable can still be read as a value:

```ini
$x = $var + 1
```

but cannot be assigned a new value after its declaration. Direct assignment attempts are rejected with a parser warning:

```ini
$var = 1 ; No-op
```

A locked variable is useful when a value should be initialized once and then treated as read-only.

Runtime writers, including `store`, keys and presets, also preserve locked values. Locked variables are not emitted as mutable persisted assignments; their declaration initializes them on reload.

## Persistent Settings

Use `persist` on a global or variable-pool declaration to save mutable values in the configured user settings file. A saved value is applied only while the destination remains persistent. Removing `persist` keeps the declaration's new default instead of applying the old saved assignment. Locked destinations are not restored or emitted as mutable saved values.

Unknown saved literal assignments can be retained when their mod is disabled:

```ini
[System]
clear_unknown_settings = 0
```

The supplied template sets `0`. If this key is missing, the DLL default is `1`; the removed `auto_clear_persist_vars` key does not override it. With cleanup enabled, an unknown collection gets a reload grace period. Names and value bit patterns must remain unchanged on the following reload before it is removed. A different value under the same name renews that grace period. A mod recognized again during Constants initialization can consume its saved value before the cleanup decision.

With cleanup disabled, an unchanged unknown collection does not force a file rewrite when known persistent variables are clean. Saving combines known and retained unknown assignments in one replacement transaction. A staging or replacement failure retains the original file and pending retry state; it does not report successful cleanup. A failure before reload preserves the old live configuration. A failure in cleanup after Constants preserves the newly loaded live configuration for the retry, rather than claiming the completed parsing was rolled back.

Stored values belong to the normalized user-file path. Changing the path or successfully wiping the settings does not resurrect values from a previous file. Unknown values must be complete numeric literals with valid names; malformed assignments and expressions are not preserved as unknown saved data.
