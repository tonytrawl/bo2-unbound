FROM ghcr.io/wiiu-env/devkitppc:20260225
COPY --from=ghcr.io/wiiu-env/libmocha@sha256:286a7890b237a2914c576d74d3744a11457ea088bf1c0177683d2147c287accf /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/librpxloader@sha256:416f72eb64ab09abfeedc2aa094a3caf855db978a369ac6586beb3a24965001a /artifacts $DEVKITPRO
WORKDIR /project
