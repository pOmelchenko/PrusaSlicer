# Prusa Slicer Plugin API

This is first version of Slice Plugin API, providing minimal operations to create new objects in Slicer project. 
This document should provide basic information on how plugin works in Prusa Slicer and how to create one.

## Getting started

To quickly create a hello world plugin, the Prusa Slicer CLI provides `plugin init` subcommand, so you can do 
something like this in your terminal:

```bash
cd <your_plugin_workspace>
/Applications/PrusaSlicer.app/Contents/MacOS/PrusaSlicer plugin init com.example.my-plugin 
```

Note: the snippet above is from macos and PrusaSlicer installed as ` /Applications/PrusaSlicer.app`, you may need
to provide path to you installation `PrusaSlicer` executable.

This command above will start interactive CLI wizard asking info about your new plugin and will create single lua file
with hello world like plugin in `com.example.my-plugin`. The info you entered can be changed in `manifest.json` file. 
Tip: for license field you can use Tab key to complete (or list known license identifiers).

To run the plugin in PrusaSlicer you will need to symlink (or copy) the `com.example.my-plugin` 
to `<config_dir>/lua/com.example.my-plugin` and run Plugins -> Rescan menu item command in Prusa Slicer. 


## Plugin anatomy

- Plugins are grouped into _plugin bundles_ (a directory, e.g. `com.prusa3d.slicer.calibratuin`)
- Plugin bundles contains `manifest.json` metadata file and one or more plugins
- Each plugin is single .lua file located under specific directory (e.g. `(datadir)/lua` or `(configdir)/lua`).
- Plugin file has to define `info` variable with description of the plugin.
- Plugin file has to define `execute` function, that runs the plugin logic.

### Plugin Bundle Metadata `manifest.json`

The `manifest.json` file describes the plugin bundle.

Here is an example of bundled plugin manifest:

```
{
	"id": "com.prusa3d.slicer.calibration",
	"name": "Calibration patterns",
	"license": "AGPL-3.0-only",
	"min_slicer_version": "3.0.0",
	"version": "1.0.0",
	"author": "prusa3d",
	"description": "Calibration patterns",
	"required_apis": {
		"project.plugin": "1.0.0"
	}
}
```

This is list of recognized `manifest.json` fields. 

| Key                  | Required | Description                                                                         |
|:---------------------|:---------|:------------------------------------------------------------------------------------|
| `id`                 | Yes      | Unique identifer of plugin bundle in reverse DNS form  e.g. `com.example.my-plugin` |
| `name`               | Yes      | Human readable name of plugin bundle                                                |
| `license`            | Yes      | [SPDX identifier](https://spdx.org/licenses/) of license                            |
| `min_slicer_version` | Yes      | Minimal version of Prusa Slicer (e.g. `3.0.0`)                                      |
| `version`            | Yes      | Version of the plugin bundle                                                        |
| `author`             | Yes      | Unique author identifier (e.g. Prusa Account handle)                                |
| `description`        | No       | Description of the plugin bundle                                                    |
| `required_apis`      | Yes      | Map of Plugin APIs (key) and its required minimal version (value)                   |
| `category`           | No       | Category identifier                                                                 |
| `web`                | No       | Plugin bundle hompage web URL                                                       |
| `repo`               | No       | Plugin bundle source code repository URL                                            |                


### Plugin Metadata `info` structure

The table `info` describes plugin with following keys:
- `id` (string) plugin unique identifier, recommended is reverse domain name like notation
- `type` (string) type of plugin, at the moment only `'project.plugin'` is allowed.
- `title` (string) displayed plugin name
- `menu` (string) menu item path to register the plugin under _Plugins_ menu item (e.g. `Calibration/My cool pattern`)
- `params` (array) list of parameter descriptions with following keys:
  - `name` (string) name of key in table as first argument passed to the `execute()` function.
  - `label` (string) displayed name in UI 
  - `type` (string) type of value / UI control, allowed values are:
    - `float` (UI: number input)
    - `int` (UI: number input)
    - `bool` (UI: checkbox)
  - `default` (number or string) default value

### Optional dialog extensions (development host)

The dialog patch adds optional `info.description` (wrapped instructions),
`info.dialog_width` (400–1000 layout units) and `info.input_width` (minimum
string-input width, clamped to 100–600). Tall forms scroll while Run and Cancel
remain accessible. Width hints should leave room for labels.

Each parameter may specify `visible_if = "boolean_parameter_name"`. Its row is
shown while that boolean is true. Hiding a row preserves its value and still
passes it to `execute`; plugins must validate selections independently of
visibility. Missing or non-boolean dependencies leave the row visible.

During execution, `api.show_result(summary, details)` displays plain text in the
same dialog. Details are initially collapsed. Back returns to the retained form
without executing the plugin, and Close dismisses the result. A plugin that
does not call this function retains the normal close-on-Run behavior.
For compatibility, check `type(api.show_result) == "function"` before calling.
This is a presentation API and does not confirm preset writes or slicing state.

Plugins may define an optional `describe()` function returning one plain-text
string. On each menu opening the host reloads the script in a fresh Lua state
with the project API and calls `describe()` before displaying the form. The text
appears above the instructions and is also retained on the result page. It is
read-only UI context, never an editable/saved parameter or an argument to
`execute()`. Back retains it; reopening recomputes it for the current project.
An empty string hides the context. Callback errors or non-string results show
an unavailable-context notice and log the error; the form remains usable.

Keep top-level code and `describe()` free of project changes. Opening or
cancelling a form must not apply settings, insert objects or emit a calculation
result; `execute()` is not called until Run. Hosts without this extension ignore
`describe()`. Plugins without it retain their original loading behavior.

`api.project:current_bed():material_preset_info(slot_idx)` returns a copied table
with the selected material preset's `id` and `name`. Slots are zero-based, as
for `material_presets()`. An invalid slot raises a Lua error. The data describes
the active selection; it does not identify a physical spool or certify saved
preset persistence. Editing the returned table does not change the preset.

```lua
function describe()
    local ok, preset = pcall(function()
        return api.project:current_bed():material_preset_info(0)
    end)
    return "Filament preset: " .. (ok and preset.name or "unavailable") .. " (slot 1)"
end
```

### Plugin `execute` function

The function `execute(params)` takes single argument,a table based upon description in the `info.params`. 
Values was filled prior calling the function, by user in UI constructed according the same description.


### Complete minimal example

This is the legendary hello world as a Slicer Plugin:

```lua
info = {
    id = "com.prusa3d.slicer.hello_world",
    type = "project.plugin",
    title = "Hello world",
    menu = "Minimal/Hello world",
    params = {
        {name = "num", label = "Your lucky number", type = "int", default = 42}
    }
}

function execute(params) 
    print("Hello no " .. params.num .. "!")
end
```

Once scanned, it should appear in the main menu under _Plugins_ → _Minimal_ → _Hello world_. 
After activation a simple UI will appear, with single input item of given label, so the user can pass an integer number.
The number can be than read by script as `params.num` in the `print` statement.

### Security model

The plugin runtime is *intentionally limited and sandboxed*. 

There are two main restrictions to be aware of:
- no standard `os` and `io` modules are available,
- plugin can access (via `emboss_svg`, `load_stl` and `require`) only files that are in the same directory 
  as the plugin .lua file itself. 

## Plugin API

Plugin API reference is located [here](https://prusa.io/ps-plugins/)

## Plugin distribution

At the moment plugins can be distributed as signed zip files. The plugin author needs to generate her public and private 
RSA keys to create the plugin distribution zip. There is `PrusaSlicer plugin keygen` utility (or you can use `openssl` 
CLI tools). Generating keys is one-time action, you don't need to do again for another plugin bundle. You will need to 
distribute your *public* key named as `<author>.pem`, where `<author>` is value of `author` field in `manifest.json` file.
The *public key* file distribution is again a one-time action.

Following command generates for you private key (the one to **keep secret**) in file `the.author.private.pem`, 
and public key (the one to *distribute*) in file `the.author.public.pem`:

```
<path to you installation>/PrusaSlicer plugin keygen -P the.author.private.pem -p the.author.public.pem
```

Finally, to create sign zip file to distribute the plugin to users, you can run following command (assuming the files 
are named same as in the example commands above):

```
<path to you installation>/PrusaSlicer plugin sign -P the.author.private.pem com.example.my-plugin
```

The output of this command is `com.example.my-plugin.zip` file to distribute.
