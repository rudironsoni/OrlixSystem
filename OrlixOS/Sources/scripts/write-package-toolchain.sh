#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
	printf 'usage: %s TOOLCHAIN_DIR\n' "$0" >&2
	exit 2
fi

toolchain_dir="$1"

: "${ORLIXOS_CC:?}"
: "${ORLIXOS_LD:?}"
: "${ORLIXOS_AR:?}"
: "${ORLIXOS_RANLIB:?}"
: "${ORLIXOS_NM:?}"
: "${ORLIXOS_STRIP:?}"
: "${ORLIXOS_OBJDUMP:?}"
: "${ORLIXOS_READELF:?}"
: "${ORLIXOS_MLIBC_SYSROOT:?}"
: "${ORLIXOS_MLIBC_HEADERS:?}"
: "${ORLIXOS_MLIBC_RTLIB:?}"
: "${ORLIXOS_HOSTED_USER_BASE_ADDRESS:?}"
: "${ORLIXOS_PACKAGE_CODE_MODEL_FLAG:?}"

mkdir -p "$toolchain_dir"

cat > "$toolchain_dir/aarch64-linux-gnu-gcc" <<'EOF'
#!/bin/bash
set -euo pipefail

: "${ORLIXOS_CC:?}"
: "${ORLIXOS_MLIBC_SYSROOT:?}"
: "${ORLIXOS_MLIBC_HEADERS:?}"
: "${ORLIXOS_MLIBC_RTLIB:?}"
: "${ORLIXOS_HOSTED_USER_BASE_ADDRESS:?}"
: "${ORLIXOS_PACKAGE_TOOLCHAIN_DIR:?}"
: "${ORLIXOS_PACKAGE_CODE_MODEL_FLAG:?}"

link=1
program_link=1
output=
next_output=0

for arg in "$@"; do
	if [ "$next_output" -eq 1 ]; then
		output="$arg"
		next_output=0
		continue
	fi

	case "$arg" in
		-c|-E|-S|-print-search-dirs|-print-prog-name=*)
			link=0
			;;
		-r|-shared)
			program_link=0
			;;
		-o)
			next_output=1
			;;
	esac
done

common=(
	--target=aarch64-linux-gnu
	-B "$ORLIXOS_PACKAGE_TOOLCHAIN_DIR"
	"--sysroot=$ORLIXOS_MLIBC_SYSROOT"
	-isystem "$ORLIXOS_MLIBC_HEADERS"
	-D_GNU_SOURCE
	-fhosted
	-fno-builtin
	-ffixed-x18
	"$ORLIXOS_PACKAGE_CODE_MODEL_FLAG"
)

if [ "$link" -eq 1 ] && [ "$program_link" -eq 1 ] && [ "${output##*.}" != la ]; then
	exec "$ORLIXOS_CC" "${common[@]}" "$@" \
		-static -fuse-ld=lld -nostdlib \
		-Wl,--gc-sections \
		-Wl,--image-base="$ORLIXOS_HOSTED_USER_BASE_ADDRESS" \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/crt1.o" \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/crti.o" \
		-Wl,--start-group \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/libc.a" \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/libm.a" \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/libpthread.a" \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/libssp_nonshared.a" \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/libssp.a" \
		"$ORLIXOS_MLIBC_RTLIB" \
		-Wl,--end-group \
		"$ORLIXOS_MLIBC_SYSROOT/usr/lib/crtn.o"
fi

if [ "$link" -eq 1 ]; then
	exec "$ORLIXOS_CC" "${common[@]}" "$@" -fuse-ld=lld -nostdlib
fi

exec "$ORLIXOS_CC" "${common[@]}" "$@"
EOF

chmod +x "$toolchain_dir/aarch64-linux-gnu-gcc"

ln -sf "$ORLIXOS_LD" "$toolchain_dir/ld"
ln -sf "$ORLIXOS_LD" "$toolchain_dir/aarch64-linux-gnu-ld"
ln -sf "$ORLIXOS_AR" "$toolchain_dir/aarch64-linux-gnu-ar"
ln -sf "$ORLIXOS_RANLIB" "$toolchain_dir/aarch64-linux-gnu-ranlib"
ln -sf "$ORLIXOS_NM" "$toolchain_dir/aarch64-linux-gnu-nm"
ln -sf "$ORLIXOS_STRIP" "$toolchain_dir/aarch64-linux-gnu-strip"
ln -sf "$ORLIXOS_OBJDUMP" "$toolchain_dir/aarch64-linux-gnu-objdump"
ln -sf "$ORLIXOS_READELF" "$toolchain_dir/aarch64-linux-gnu-readelf"
