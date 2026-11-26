# ACE

ACE (the Answer Constraint Engine) is Woodley Packard's processor for
DELPH-IN HPSG grammars. It parses, generates and transfers with grammars
written in TDL, such as the English Resource Grammar.

This repository holds a copy of the ACE source. The version is in `version.h`.

The original source and documentation is available from <https://sweaglesw.org/linguistics/ace/>

## Documentation

There is more documentation on the DELPH-IN site:

- [AceTop](https://delph-in.github.io/docs/tools/AceTop/): overview and index
- [AceInstall](https://delph-in.github.io/docs/tools/AceInstall/): getting or building ACE
- [AceUse](https://delph-in.github.io/docs/tools/AceUse/): compiling a grammar, parsing and generating
- [AceOptions](https://delph-in.github.io/docs/tools/AceOptions/): command-line options
- [AceConfiguration](https://delph-in.github.io/docs/tools/AceConfiguration/): the grammar configuration file (`ace/config.tdl`)
- [AceLui](https://delph-in.github.io/docs/tools/AceLui/): using ACE with LUI
- [AceFaq](https://delph-in.github.io/docs/tools/AceFaq/): frequently asked questions

`RELEASE-NOTES` in this directory has the version history.

## Building

ACE needs:

- [librepp](https://github.com/delph-in/librepp), Woodley Packard's C
  implementation of REPP (the Regular Expression Pre-Processor; see
  [ReppTop](https://delph-in.github.io/docs/garage/ReppTop/)). ACE uses it to
  tokenize input with a grammar's REPP rules. Build and install it first.
- Boost.Regex (headers and library).
- For [incr tsdb()] support, which the `Makefile` turns on by default,
  `libitsdb` and `libpvm3` from a LOGON tree; set `LOGONROOT` to that tree.

Then:

```sh
make          # builds ace, libace.so and libace.a
make install  # to PREFIX, by default /usr/local
```

The paths and flags for these libraries are set at the top of the
`Makefile`. [AceInstall](https://delph-in.github.io/docs/tools/AceInstall/)
has fuller instructions.

## Reporting problems

Please use the [issue tracker](https://github.com/delph-in/ace/issues).

## License

ACE is released under the MIT license. See `LICENSE`, which also gives the
licenses of the code ACE includes from other projects.
