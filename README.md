# mdp

[![CI](https://github.com/p2sr/mdp/workflows/CI/badge.svg)](https://github.com/p2sr/mdp/actions?query=workflow%3ACI+branch%3Amaster)
[![CD](https://github.com/p2sr/mdp/workflows/CD/badge.svg)](https://github.com/p2sr/mdp/actions?query=workflow%3ACD+branch%3Amaster)

`mdp` is a very simple demo parser for Portal 2 demos which extracts information injected by [SAR].

[SAR]: https://github.com/p2sr/SourceAutoRecord

## Usage

After building `mdp`, place it in a directory containing the following files:

- `cmd_whitelist.txt` (whitelist of allowed command prefixes)
- `cvar_whitelist.txt` (whitelist of allowed initial cvar values)
- `expected_maps.txt` (BSP names expected to be in demos being checked)
- `sar_whitelist.txt` (whitelist of SAR CRC32 checksums)
- `filesum_whitelist.txt` (whitelist of CRC32 checksums for checked files)
- `config.txt` (general configuration)

Then, place all the demos in a subdirectory `demos/`, and run `mdp`. It will create two files: `errors.txt` and `output.txt`.

### `cmd_whitelist.txt`

This is a whitelist of allowed command prefixes which will be omitted from the parser output. Example:

```
sar_speedrun_
sar_autorecord
sar_record_at
sar_record_prefix
sar_fast_load_preset
ui_loadingscreen_transition_time 0.0
ui_loadingscreen_fadein_time 0.0
```

### `cvar_whitelist.txt`

This is a whitelist for allowed values for cvars to initially contain. Lines may be formatted as `cvar val` to allow a specific value, or `cvar` to allow any
value. Initial values matching the whitelist are omitted from output. Cvars whose value is the game's default are not included in demos. Example:

```
sar_speedrun_offset
sar_record_at 0
```

### `expected_maps.txt`

This is a list of map names (one per line) which are all expected to be in the demos. Any of these maps which were not visited in any demo are added to the end
of `output.txt`.

### `sar_whitelist.txt`

This is a whitelist of SAR CRC32 checksums to allow (one per line). Demos will report if the SAR checksum doesn't match a whitelisted value.

### `filesum_whitelist.txt`

This is a whitelist of file checksums - SAR adds checksums for certain important files like custom VPKs and vscripts. A file can be excluded from appearing by
placing its name in this file, or a specific checksum can be allowed for it by writing its name, then a space, then the expected checksum. Allowed values are
omitted from output.

### `config.txt`

This file contains a few configuration settings, configured using lines of the format `key value`. Possible options:

- `file_sum_mode [0/1/2]`. 0 = don't show, 1 = show not matching, 2 (default) = show not matching or not present.
- `initial_cvar_mode [0/1/2]`. 0 = don't show, 1 = show not matching, 2 (default) = show not matching or not present.
- `show_passing_checksums [0/1]`. Defaults to 0 (off).
- `show_wait [0/1]`. Defaults to 1 (on).
- `show_splits [0/1]`. Defaults to 1 (on). Shows splits when a speedrun finishes.
