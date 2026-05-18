# Orlix Mach-O Section Probes

These files are probe-only inputs for investigating how Linux section classes could be represented in Mach-O objects.

They are not part of `OrlixKernel.a`, are not linked into `OrlixKernel.framework`, and do not prove Linux boot or runtime behavior.

Run the probe directly or through the private kernel lint target:

```sh
tools/investigations/orlix-mach-o-sections/run-probe.sh --profile appstore
```

The probe uses explicit Mach-O section names under `__ORLIX`. It does not imply that generic Linux `__section()` mapping is accepted for the product lane.
