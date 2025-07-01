# vstshill processing guide

cross-platform vst3 host for analyzing and processing audio through plugins.

## build

```bash
cmake -B build
cmake --build build --parallel
```

## commands

### scan
find available vst3 plugins:

```bash
./build/bin/vstshill scan
./build/bin/vstshill scan -d          # detailed info
```

### inspect
analyze plugin capabilities:

```bash
./build/bin/vstshill inspect plugin.vst3
```

### parameters
list plugin parameters:

```bash
./build/bin/vstshill parameters plugin.vst3
```

### gui
open plugin editor:

```bash
./build/bin/vstshill gui plugin.vst3
```

### process
main audio processing command.

## basic usage

### synthesizers (instrument mode)
```bash
# basic synthesis (10 seconds)
./build/bin/vstshill process -o output.wav synth.vst3

# custom duration
./build/bin/vstshill process -o output.wav -t 5 synth.vst3

# with parameters
./build/bin/vstshill process -o output.wav -p "cutoff:0.8" synth.vst3
```

### effects
```bash
# basic effect processing
./build/bin/vstshill process -i input.wav -o output.wav effect.vst3

# multiple inputs
./build/bin/vstshill process -i left.wav -i right.wav -o stereo.wav effect.vst3
```

## options

### input/output
```bash
-i, --input          input audio files (multiple allowed)
-o, --output         output audio file (required)
-y, --overwrite      overwrite existing output file
```

### processing
```bash
-r, --sample-rate    output sample rate (default: input or 44100)
-b, --block-size     processing block size (default: 512)  
-d, --bit-depth      output bit depth: 16, 24, 32 (default: 32)
-t, --duration       duration for instrument mode (default: 10)
```

### plugin control
```bash
-p, --param          parameter as name:value (multiple allowed)
-a, --automation     json automation file
```

### utility
```bash
-n, --dry-run        validate setup only
-q, --quiet          minimal output
--progress           detailed progress info
```

## examples

### quick synthesis
```bash
# 3 seconds at 48khz
./build/bin/vstshill process -o quick.wav -t 3 -r 48000 synth.vst3

# with overwrite and 16-bit output
./build/bin/vstshill process -o test.wav -y -t 2 -d 16 synth.vst3
```

### effect processing
```bash
# basic tremolo
./build/bin/vstshill process -i input.wav -o tremolo.wav tremolo.vst3

# tremolo with settings
./build/bin/vstshill process -i input.wav -o tremolo.wav tremolo.vst3 \
  -p "rate:5.0" -p "depth:80.0"
```

### automation
```bash
./build/bin/vstshill process -i input.wav -o auto.wav filter.vst3 \
  -a automation.json
```

automation.json:
```json
{
  "cutoff": {
    "0.0": 0.2,
    "2.0": 0.8,
    "4.0": 0.2  
  }
}
```

### processing chains
```bash
# generate → process → process
./build/bin/vstshill process -o raw.wav synth.vst3
./build/bin/vstshill process -i raw.wav -o fx1.wav tremolo.vst3
./build/bin/vstshill process -i fx1.wav -o final.wav reverb.vst3 -y
```

### validation
```bash
# check setup without processing
./build/bin/vstshill process -n -o test.wav -i input.wav effect.vst3

# quiet mode for scripts
./build/bin/vstshill process -q -o output.wav synth.vst3
```

### high quality
```bash
./build/bin/vstshill process -i input.wav -o hq.wav effect.vst3 \
  -r 96000 -d 32 -b 256
```

## plugin locations

### macos
- `/Library/Audio/Plug-Ins/VST3/`
- `~/Library/Audio/Plug-Ins/VST3/`

### windows  
- `C:\Program Files\Common Files\VST3\`
- `C:\Program Files\VST3\`

### linux
- `/usr/lib/vst3/`
- `~/.vst3/`

## troubleshooting

### validation
```bash
# test plugin loads
./build/bin/vstshill inspect plugin.vst3

# test setup
./build/bin/vstshill process -n -o test.wav plugin.vst3
```

### silent output
- ensure instrument plugins have no input specified
- verify effect plugins have valid input files
- check parameter settings

### crashes
- some plugins have compatibility issues
- try different plugins to isolate problems
- use `-v` for verbose output

### parameters
```bash
# list available parameters
./build/bin/vstshill parameters plugin.vst3

# parameter names are case-sensitive
```