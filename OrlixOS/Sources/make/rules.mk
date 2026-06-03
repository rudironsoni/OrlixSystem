test coreutils-test: $(ORLIXOS_COREUTILS_TEST_INITRAMFS)
	@set -euo pipefail; \
	run_log="$(ORLIXOS_COREUTILS_TEST_RUN_LOG)"; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_RUN_LOG))"; \
	$(KERNEL_MAKE) run PROFILE="$(PROFILE)" type=coreutils libc=orlixmlibc ORLIX_KERNEL_TEST_INITRAMFS_INPUT="$(ORLIXOS_COREUTILS_TEST_INITRAMFS)" ORLIX_KERNEL_RUN_UNTIL_MARKER="ORLIX-COREUTILS-TEST-END" ORLIX_KERNEL_RUN_TIMEOUT_SECONDS="$(ORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS)" | tee "$$run_log"; \
	runtime_log="$$(awk -F'"' '/runtimeLogPath/ { print $$4 }' "$$run_log" | tail -n 1)"; \
	[ -n "$$runtime_log" ] || { echo "missing OrlixTerminal runtime log path in $$run_log" >&2; exit 1; }; \
	for _ in $$(seq 1 3600); do \
		grep -F -q 'ORLIX-COREUTILS-TEST-END' "$$runtime_log" && break; \
		sleep 1; \
	done; \
	grep -F -q 'ORLIX-COREUTILS-TEST-END failures=0 skips=0' "$$runtime_log" || { echo "Coreutils upstream tests did not complete successfully: $$runtime_log" >&2; exit 1; }; \
	echo "verified upstream Coreutils tests in simulator log: $$runtime_log"

$(ORLIXOS_BASH_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch Bash source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify Bash source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_BASH_ARCHIVE)" ]; then curl -fL "$(BASH_URL)" -o "$(ORLIXOS_BASH_ARCHIVE)"; fi; \
	echo "upstream Bash archive ready: $(ORLIXOS_BASH_ARCHIVE)"

$(ORLIXOS_BASH_ARCHIVE_STAMP): $(ORLIXOS_BASH_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify Bash source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(BASH_SHA256)" "$(ORLIXOS_BASH_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_BASH_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_BASH_ARCHIVE_STAMP)"; \
	echo "upstream Bash ready: $(ORLIXOS_BASH_ARCHIVE)"

$(ORLIXOS_BASH_SOURCE_STAMP): $(ORLIXOS_BASH_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BASH_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_BASH_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_BASH_SOURCE_STAMP)"; \
	echo "extracted Bash source: $(ORLIXOS_BASH_SRC_DIR)"

__coreutils-source: $(ORLIXOS_COREUTILS_SOURCE_STAMP)

$(ORLIXOS_COREUTILS_SOURCE_STAMP): FORCE
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_SRC_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	export PATH="$(ORLIXOS_COREUTILS_BOOTSTRAP_PATH)"; \
	command -v git >/dev/null 2>&1 || { echo "git is required to clone Coreutils source" >&2; exit 1; }; \
	command -v autoconf >/dev/null 2>&1 || { echo "autoconf is required to bootstrap Coreutils from git" >&2; exit 1; }; \
	command -v automake >/dev/null 2>&1 || { echo "automake is required to bootstrap Coreutils from git" >&2; exit 1; }; \
	command -v autopoint >/dev/null 2>&1 || { echo "autopoint is required to bootstrap Coreutils from git" >&2; exit 1; }; \
	command -v bison >/dev/null 2>&1 || { echo "GNU bison is required to bootstrap Coreutils from git" >&2; exit 1; }; \
	command -v makeinfo >/dev/null 2>&1 || { echo "makeinfo is required to bootstrap Coreutils from git" >&2; exit 1; }; \
	command -v texi2pdf >/dev/null 2>&1 || { echo "texi2pdf is required to bootstrap Coreutils from git" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_COREUTILS_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	git clone --filter=blob:none --no-tags --no-checkout "$(COREUTILS_GIT_URL)" "$(ORLIXOS_COREUTILS_SRC_DIR)"; \
	git -C "$(ORLIXOS_COREUTILS_SRC_DIR)" fetch --depth 1 --no-tags origin "$(COREUTILS_GIT_COMMIT)"; \
	git -C "$(ORLIXOS_COREUTILS_SRC_DIR)" checkout --detach "$(COREUTILS_GIT_COMMIT)"; \
	git -C "$(ORLIXOS_COREUTILS_SRC_DIR)" config submodule.gnulib.url "$(COREUTILS_GNULIB_GIT_URL)"; \
	git -C "$(ORLIXOS_COREUTILS_SRC_DIR)" submodule update --init --depth 1 --recommend-shallow --recursive --jobs 4; \
	actual="$$(git -C "$(ORLIXOS_COREUTILS_SRC_DIR)" rev-parse HEAD)"; \
	[ "$$actual" = "$(COREUTILS_GIT_COMMIT)" ] || { echo "Coreutils clone resolved $$actual, expected $(COREUTILS_GIT_COMMIT)" >&2; exit 1; }; \
	cd "$(ORLIXOS_COREUTILS_SRC_DIR)"; \
	./bootstrap --skip-po --no-git --gnulib-srcdir=gnulib; \
	touch "$(ORLIXOS_COREUTILS_SOURCE_STAMP)"; \
	echo "freshly cloned upstream Coreutils source: $(ORLIXOS_COREUTILS_SRC_DIR) ($(COREUTILS_GIT_REF) $(COREUTILS_GIT_COMMIT))"

$(ORLIXOS_GREP_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch grep source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify grep source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_GREP_ARCHIVE)" ]; then curl -fL "$(GREP_URL)" -o "$(ORLIXOS_GREP_ARCHIVE)"; fi; \
	echo "upstream grep archive ready: $(ORLIXOS_GREP_ARCHIVE)"

$(ORLIXOS_GREP_ARCHIVE_STAMP): $(ORLIXOS_GREP_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify grep source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(GREP_SHA256)" "$(ORLIXOS_GREP_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_GREP_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_GREP_ARCHIVE_STAMP)"; \
	echo "upstream grep ready: $(ORLIXOS_GREP_ARCHIVE)"

$(ORLIXOS_GREP_SOURCE_STAMP): $(ORLIXOS_GREP_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_GREP_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_GREP_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_GREP_SOURCE_STAMP)"; \
	echo "extracted grep source: $(ORLIXOS_GREP_SRC_DIR)"

$(ORLIXOS_SED_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch sed source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify sed source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_SED_ARCHIVE)" ]; then curl -fL "$(SED_URL)" -o "$(ORLIXOS_SED_ARCHIVE)"; fi; \
	echo "upstream sed archive ready: $(ORLIXOS_SED_ARCHIVE)"

$(ORLIXOS_SED_ARCHIVE_STAMP): $(ORLIXOS_SED_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify sed source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(SED_SHA256)" "$(ORLIXOS_SED_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_SED_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_SED_ARCHIVE_STAMP)"; \
	echo "upstream sed ready: $(ORLIXOS_SED_ARCHIVE)"

$(ORLIXOS_SED_SOURCE_STAMP): $(ORLIXOS_SED_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_SED_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_SED_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_SED_SOURCE_STAMP)"; \
	echo "extracted sed source: $(ORLIXOS_SED_SRC_DIR)"

$(ORLIXOS_DIFFUTILS_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch diffutils source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify diffutils source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_DIFFUTILS_ARCHIVE)" ]; then curl -fL "$(DIFFUTILS_URL)" -o "$(ORLIXOS_DIFFUTILS_ARCHIVE)"; fi; \
	echo "upstream diffutils archive ready: $(ORLIXOS_DIFFUTILS_ARCHIVE)"

$(ORLIXOS_DIFFUTILS_ARCHIVE_STAMP): $(ORLIXOS_DIFFUTILS_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify diffutils source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(DIFFUTILS_SHA256)" "$(ORLIXOS_DIFFUTILS_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_DIFFUTILS_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_DIFFUTILS_ARCHIVE_STAMP)"; \
	echo "upstream diffutils ready: $(ORLIXOS_DIFFUTILS_ARCHIVE)"

$(ORLIXOS_DIFFUTILS_SOURCE_STAMP): $(ORLIXOS_DIFFUTILS_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_DIFFUTILS_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_DIFFUTILS_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_DIFFUTILS_SOURCE_STAMP)"; \
	echo "extracted diffutils source: $(ORLIXOS_DIFFUTILS_SRC_DIR)"

$(ORLIXOS_GAWK_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch gawk source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify gawk source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_GAWK_ARCHIVE)" ]; then curl -fL "$(GAWK_URL)" -o "$(ORLIXOS_GAWK_ARCHIVE)"; fi; \
	echo "upstream gawk archive ready: $(ORLIXOS_GAWK_ARCHIVE)"

$(ORLIXOS_GAWK_ARCHIVE_STAMP): $(ORLIXOS_GAWK_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify gawk source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(GAWK_SHA256)" "$(ORLIXOS_GAWK_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_GAWK_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_GAWK_ARCHIVE_STAMP)"; \
	echo "upstream gawk ready: $(ORLIXOS_GAWK_ARCHIVE)"

$(ORLIXOS_GAWK_SOURCE_STAMP): $(ORLIXOS_GAWK_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_GAWK_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_GAWK_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_GAWK_SOURCE_STAMP)"; \
	echo "extracted gawk source: $(ORLIXOS_GAWK_SRC_DIR)"

$(ORLIXOS_FINDUTILS_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch findutils source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify findutils source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_FINDUTILS_ARCHIVE)" ]; then curl -fL "$(FINDUTILS_URL)" -o "$(ORLIXOS_FINDUTILS_ARCHIVE)"; fi; \
	echo "upstream findutils archive ready: $(ORLIXOS_FINDUTILS_ARCHIVE)"

$(ORLIXOS_FINDUTILS_ARCHIVE_STAMP): $(ORLIXOS_FINDUTILS_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify findutils source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(FINDUTILS_SHA256)" "$(ORLIXOS_FINDUTILS_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_FINDUTILS_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_FINDUTILS_ARCHIVE_STAMP)"; \
	echo "upstream findutils ready: $(ORLIXOS_FINDUTILS_ARCHIVE)"

$(ORLIXOS_FINDUTILS_SOURCE_STAMP): $(ORLIXOS_FINDUTILS_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_FINDUTILS_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_FINDUTILS_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_FINDUTILS_SOURCE_STAMP)"; \
	echo "extracted findutils source: $(ORLIXOS_FINDUTILS_SRC_DIR)"

$(ORLIXOS_UTIL_LINUX_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch util-linux source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify util-linux source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_UTIL_LINUX_ARCHIVE)" ]; then curl -fL "$(UTIL_LINUX_URL)" -o "$(ORLIXOS_UTIL_LINUX_ARCHIVE)"; fi; \
	echo "upstream util-linux archive ready: $(ORLIXOS_UTIL_LINUX_ARCHIVE)"

$(ORLIXOS_UTIL_LINUX_ARCHIVE_STAMP): $(ORLIXOS_UTIL_LINUX_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify util-linux source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(UTIL_LINUX_SHA256)" "$(ORLIXOS_UTIL_LINUX_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_UTIL_LINUX_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_UTIL_LINUX_ARCHIVE_STAMP)"; \
	echo "upstream util-linux ready: $(ORLIXOS_UTIL_LINUX_ARCHIVE)"

$(ORLIXOS_UTIL_LINUX_SOURCE_STAMP): $(ORLIXOS_UTIL_LINUX_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_UTIL_LINUX_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_UTIL_LINUX_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_UTIL_LINUX_SOURCE_STAMP)"; \
	echo "extracted util-linux source: $(ORLIXOS_UTIL_LINUX_SRC_DIR)"

$(ORLIXOS_PERL_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch perl source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify perl source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_PERL_ARCHIVE)" ]; then curl -fL "$(PERL_URL)" -o "$(ORLIXOS_PERL_ARCHIVE)"; fi; \
	echo "upstream perl archive ready: $(ORLIXOS_PERL_ARCHIVE)"

$(ORLIXOS_PERL_ARCHIVE_STAMP): $(ORLIXOS_PERL_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify perl source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(PERL_SHA256)" "$(ORLIXOS_PERL_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_PERL_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_PERL_ARCHIVE_STAMP)"; \
	echo "upstream perl ready: $(ORLIXOS_PERL_ARCHIVE)"

$(ORLIXOS_PERL_CROSS_ARCHIVE):
	@set -euo pipefail; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch perl-cross source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify perl-cross source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_PERL_CROSS_ARCHIVE)" ]; then curl -fL "$(PERL_CROSS_URL)" -o "$(ORLIXOS_PERL_CROSS_ARCHIVE)"; fi; \
	echo "upstream perl-cross archive ready: $(ORLIXOS_PERL_CROSS_ARCHIVE)"

$(ORLIXOS_PERL_CROSS_ARCHIVE_STAMP): $(ORLIXOS_PERL_CROSS_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify perl-cross source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(PERL_CROSS_SHA256)" "$(ORLIXOS_PERL_CROSS_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_PERL_CROSS_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_PERL_CROSS_ARCHIVE_STAMP)"; \
	echo "upstream perl-cross ready: $(ORLIXOS_PERL_CROSS_ARCHIVE)"

$(ORLIXOS_PERL_SOURCE_STAMP): $(ORLIXOS_PERL_ARCHIVE_STAMP) $(ORLIXOS_PERL_CROSS_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_PERL_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_PERL_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_PERL_CROSS_ARCHIVE)" -C "$(ORLIXOS_PERL_SRC_DIR)" --strip-components=1; \
	patch -d "$(ORLIXOS_PERL_SRC_DIR)" -p1 < "$(PROJECT_DIR)/Sources/patches/perl-cross-1.6.2-darwin-readelf-size.patch"; \
	patch -d "$(ORLIXOS_PERL_SRC_DIR)" -p1 < "$(PROJECT_DIR)/Sources/patches/perl-cross-1.6.2-byteorder-fallback.patch"; \
	touch "$(ORLIXOS_PERL_SOURCE_STAMP)"; \
	echo "extracted perl source: $(ORLIXOS_PERL_SRC_DIR)"

$(ORLIXOS_JQ_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch jq source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify jq source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_JQ_ARCHIVE)" ]; then curl -fL "$(JQ_URL)" -o "$(ORLIXOS_JQ_ARCHIVE)"; fi; \
	echo "upstream jq archive ready: $(ORLIXOS_JQ_ARCHIVE)"

$(ORLIXOS_JQ_ARCHIVE_STAMP): $(ORLIXOS_JQ_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify jq source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(JQ_SHA256)" "$(ORLIXOS_JQ_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_JQ_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_JQ_ARCHIVE_STAMP)"; \
	echo "upstream jq ready: $(ORLIXOS_JQ_ARCHIVE)"

$(ORLIXOS_JQ_SOURCE_STAMP): $(ORLIXOS_JQ_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_JQ_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_JQ_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_JQ_SOURCE_STAMP)"; \
	echo "extracted jq source: $(ORLIXOS_JQ_SRC_DIR)"

$(ORLIXOS_CURL_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch curl source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify curl source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_CURL_ARCHIVE)" ]; then curl -fL "$(CURL_URL)" -o "$(ORLIXOS_CURL_ARCHIVE)"; fi; \
	echo "upstream curl archive ready: $(ORLIXOS_CURL_ARCHIVE)"

$(ORLIXOS_CURL_ARCHIVE_STAMP): $(ORLIXOS_CURL_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify curl source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(CURL_SHA256)" "$(ORLIXOS_CURL_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_CURL_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_CURL_ARCHIVE_STAMP)"; \
	echo "upstream curl ready: $(ORLIXOS_CURL_ARCHIVE)"

$(ORLIXOS_CURL_SOURCE_STAMP): $(ORLIXOS_CURL_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_CURL_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_CURL_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_CURL_SOURCE_STAMP)"; \
	echo "extracted curl source: $(ORLIXOS_CURL_SRC_DIR)"

$(ORLIXOS_NCURSES_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch ncurses source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify ncurses source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_NCURSES_ARCHIVE)" ]; then curl -fL "$(NCURSES_URL)" -o "$(ORLIXOS_NCURSES_ARCHIVE)"; fi; \
	echo "upstream ncurses archive ready: $(ORLIXOS_NCURSES_ARCHIVE)"

$(ORLIXOS_NCURSES_ARCHIVE_STAMP): $(ORLIXOS_NCURSES_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify ncurses source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(NCURSES_SHA256)" "$(ORLIXOS_NCURSES_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_NCURSES_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_NCURSES_ARCHIVE_STAMP)"; \
	echo "upstream ncurses ready: $(ORLIXOS_NCURSES_ARCHIVE)"

$(ORLIXOS_NCURSES_SOURCE_STAMP): $(ORLIXOS_NCURSES_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_NCURSES_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_NCURSES_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_NCURSES_SOURCE_STAMP)"; \
	echo "extracted ncurses source: $(ORLIXOS_NCURSES_SRC_DIR)"

$(ORLIXOS_ZSH_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch zsh source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify zsh source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_ZSH_ARCHIVE)" ]; then curl -fL "$(ZSH_URL)" -o "$(ORLIXOS_ZSH_ARCHIVE)"; fi; \
	echo "upstream zsh archive ready: $(ORLIXOS_ZSH_ARCHIVE)"

$(ORLIXOS_ZSH_ARCHIVE_STAMP): $(ORLIXOS_ZSH_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify zsh source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(ZSH_SHA256)" "$(ORLIXOS_ZSH_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_ZSH_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_ZSH_ARCHIVE_STAMP)"; \
	echo "upstream zsh ready: $(ORLIXOS_ZSH_ARCHIVE)"

$(ORLIXOS_ZSH_SOURCE_STAMP): $(ORLIXOS_ZSH_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_ZSH_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_ZSH_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_ZSH_SOURCE_STAMP)"; \
	echo "extracted zsh source: $(ORLIXOS_ZSH_SRC_DIR)"

$(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB): $(REPO_ROOT)/OrlixMLibC/Makefile $(ORLIXOS_MLIBC_PATCHES)
	@set -euo pipefail; \
	$(MLIBC_MAKE) build PROFILE="$(PROFILE)"; \
	[ -s "$(ORLIXOS_MLIBC_SYSROOT)/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $(ORLIXOS_MLIBC_SYSROOT)/usr/lib/libc.a" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_MLIBC_RTLIB)" ] || { echo "missing Orlix compiler runtime archive: $(ORLIXOS_MLIBC_RTLIB)" >&2; exit 1; }

$(ORLIXOS_BASH_BINARY): $(ORLIXOS_BASH_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Bash; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build Bash; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build Bash; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Bash; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_BASH_BUILD_DIR)" "$(ORLIXOS_BASH_BINARY)"; \
	mkdir -p "$(ORLIXOS_BASH_BUILD_DIR)" "$(dir $(ORLIXOS_BASH_BINARY))"; \
	cd "$(ORLIXOS_BASH_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="-O2 -Wno-unknown-warning-option"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export bash_cv_getenv_redef=no; \
	export bash_cv_getcwd_malloc=yes; \
	export bash_cv_func_strchrnul_works=yes; \
	"$(ORLIXOS_BASH_SRC_DIR)/configure" --host=aarch64-linux-gnu --build="$$(uname -m)-apple-darwin" --prefix=/usr --without-bash-malloc --enable-static-link --disable-nls --disable-readline --without-installed-readline --without-curses; \
	$(MAKE) -j1 bash; \
	cp "$(ORLIXOS_BASH_BUILD_DIR)/bash" "$(ORLIXOS_BASH_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_BASH_BINARY)"; \
	file "$(ORLIXOS_BASH_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_BASH_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=bash\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(BASH_VERSION)" "$(BASH_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/bash.proof"; \
	rm -rf "$(ORLIXOS_BASH_BUILD_DIR)" "$(ORLIXOS_BASH_SRC_DIR)"; \
	echo "built Orlix Linux Bash package input: $(ORLIXOS_BASH_BINARY)"

$(ORLIXOS_COREUTILS_PROOF): $(ORLIXOS_COREUTILS_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build coreutils; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build coreutils; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build coreutils; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package coreutils; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_COREUTILS_BUILD_DIR)"; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do rm -f "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
	mkdir -p "$(ORLIXOS_COREUTILS_BUILD_DIR)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	cd "$(ORLIXOS_COREUTILS_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export PATH="$(ORLIXOS_COREUTILS_BOOTSTRAP_PATH)"; \
	export gl_cv_header_working_fcntl_h=yes; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	export gl_cv_func_strtod_works=yes; \
	"$(ORLIXOS_COREUTILS_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --without-selinux --disable-acl --disable-xattr --disable-libcap --disable-gcc-warnings; \
	$(MAKE) -j1 all PROGRAMS= LIBRARIES= MANS= INFO_DEPS=; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		source_program="$$program"; \
		if [ "$$program" = install ]; then source_program=ginstall; fi; \
		$(MAKE) -j1 "src/$$source_program" MANS= INFO_DEPS=; \
	done; \
	$(MAKE) -j1 src/getlimits; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		source_program="$$program"; \
		if [ "$$program" = install ]; then source_program=ginstall; fi; \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/$$source_program"; \
	done; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/getlimits"; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		source_program="$$program"; \
		if [ "$$program" = install ]; then source_program=ginstall; fi; \
		install -m 0755 "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/$$source_program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
	done; \
	cp "$(ORLIXOS_COREUTILS_BUILD_DIR)/src/getlimits" "$(ORLIXOS_GETLIMITS_BINARY)"; \
	file "$(ORLIXOS_GETLIMITS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETLIMITS_BINARY)" >&2; exit 1; }; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=coreutils\nprograms=%s\nversion=%s\ngit_url=%s\ngit_ref=%s\ngit_commit=%s\ngnulib_git_url=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_COREUTILS_PROGRAMS)" "$(COREUTILS_VERSION)" "$(COREUTILS_GIT_URL)" "$(COREUTILS_GIT_REF)" "$(COREUTILS_GIT_COMMIT)" "$(COREUTILS_GNULIB_GIT_URL)" > "$(ORLIXOS_COREUTILS_PROOF)"; \
	echo "built Orlix Linux coreutils package inputs: $(ORLIXOS_COREUTILS_PROGRAMS)"

$(ORLIXOS_COREUTILS_TEST_LIST): $(ORLIXOS_COREUTILS_PROOF) $(PROJECT_DIR)/Makefile
	@set -euo pipefail; \
	makefile="$(ORLIXOS_COREUTILS_BUILD_DIR)/Makefile"; \
	[ -s "$$makefile" ] || { echo "missing configured Coreutils Makefile: $$makefile" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_LIST))"; \
	raw_list="$(ORLIXOS_COREUTILS_TEST_LIST).raw"; \
	awk '\
		function flush(mode, line, i, value) { \
			gsub(/\\/, "", line); \
			for (i = 1; i <= split(line, fields, /[[:space:]]+/); i++) { \
				value = fields[i]; \
				if (value == "" || value == "=") continue; \
				gsub(/\044[(]tf[)]/, "tests/factor", value); \
				if (value ~ /^tests\//) print mode " " value; \
			} \
		} \
		/^(all_tests|factor_tests) =/ { mode = "user"; collecting = 1; flush(mode, $$0); if ($$0 !~ /\\$$/) collecting = 0; next } \
		/^all_root_tests =/ { mode = "root"; collecting = 1; flush(mode, $$0); if ($$0 !~ /\\$$/) collecting = 0; next } \
		collecting { flush(mode, $$0); if ($$0 !~ /\\$$/) collecting = 0 } \
	' "$$makefile" | sort -u > "$$raw_list"; \
	awk '\
		$$1 == "root" { root[$$2] = $$0; next } \
		$$1 == "user" { user[$$2] = $$0; next } \
		END { \
			for (test in user) if (!(test in root)) print user[test]; \
			for (test in root) print root[test]; \
		} \
	' "$$raw_list" | sort -k2,2 > "$(ORLIXOS_COREUTILS_TEST_LIST)"; \
	rm -f "$$raw_list"; \
	[ -s "$(ORLIXOS_COREUTILS_TEST_LIST)" ] || { echo "empty Coreutils upstream test list" >&2; exit 1; }; \
	cp "$(ORLIXOS_COREUTILS_BUILD_DIR)/lib/config.h" "$(ORLIXOS_COREUTILS_CONFIG_HEADER)"; \
	[ -s "$(ORLIXOS_COREUTILS_CONFIG_HEADER)" ] || { echo "missing Coreutils config snapshot: $(ORLIXOS_COREUTILS_CONFIG_HEADER)" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_COREUTILS_BUILD_DIR)"; \
	echo "wrote upstream Coreutils test list: $(ORLIXOS_COREUTILS_TEST_LIST) ($$(wc -l < "$(ORLIXOS_COREUTILS_TEST_LIST)") tests)"

$(ORLIXOS_COREUTILS_TEST_ENV): $(ORLIXOS_COREUTILS_PROOF) $(PROJECT_DIR)/Makefile
	@set -euo pipefail; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_ENV))"; \
	{ \
		printf '%s\n' '# generated from OrlixOS/Makefile package inputs'; \
		printf 'export VERSION=%s\n' "$(COREUTILS_VERSION)"; \
		printf 'export PACKAGE_VERSION=%s\n' "$(COREUTILS_VERSION)"; \
		printf 'export PERL5LIB=/usr/lib/perl5/%s\n' "$(PERL_VERSION)"; \
		printf "export built_programs='%s'\n" "$(ORLIXOS_COREUTILS_PROGRAMS)"; \
	} > "$(ORLIXOS_COREUTILS_TEST_ENV)"; \
	echo "wrote Coreutils upstream test environment: $(ORLIXOS_COREUTILS_TEST_ENV)"

$(ORLIXOS_GETLIMITS_BINARY): $(ORLIXOS_COREUTILS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_GETLIMITS_BINARY)" ] || { echo "missing upstream Coreutils getlimits helper: $(ORLIXOS_GETLIMITS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_GETLIMITS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETLIMITS_BINARY)" >&2; exit 1; }

$(ORLIXOS_FINDUTILS_PROOF): $(ORLIXOS_FINDUTILS_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build findutils; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build findutils; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build findutils; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package findutils; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_FINDUTILS_BUILD_DIR)"; \
	for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do rm -f "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
	mkdir -p "$(ORLIXOS_FINDUTILS_BUILD_DIR)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	cd "$(ORLIXOS_FINDUTILS_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export PATH="$(ORLIXOS_COREUTILS_BOOTSTRAP_PATH)"; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	"$(ORLIXOS_FINDUTILS_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --without-selinux; \
	$(MAKE) -C gl/lib -j1 all; \
	$(MAKE) -C lib -j1 all; \
	$(MAKE) -C find -j1 find; \
	$(MAKE) -C xargs -j1 xargs; \
	for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_FINDUTILS_BUILD_DIR)/$$program/$$program"; \
		install -m 0755 "$(ORLIXOS_FINDUTILS_BUILD_DIR)/$$program/$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=findutils\nprograms=%s\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_FINDUTILS_PROGRAMS)" "$(FINDUTILS_VERSION)" "$(FINDUTILS_SHA256)" > "$(ORLIXOS_FINDUTILS_PROOF)"; \
	rm -rf "$(ORLIXOS_FINDUTILS_BUILD_DIR)" "$(ORLIXOS_FINDUTILS_SRC_DIR)"; \
	echo "built Orlix Linux findutils package inputs: $(ORLIXOS_FINDUTILS_PROGRAMS)"

$(ORLIXOS_FIND_BINARY): $(ORLIXOS_FINDUTILS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_FIND_BINARY)" ] || { echo "missing findutils find package input: $(ORLIXOS_FIND_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_FIND_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_FIND_BINARY)" >&2; exit 1; }

$(ORLIXOS_XARGS_BINARY): $(ORLIXOS_FINDUTILS_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_XARGS_BINARY)" ] || { echo "missing findutils xargs package input: $(ORLIXOS_XARGS_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_XARGS_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_XARGS_BINARY)" >&2; exit 1; }

$(ORLIXOS_UTIL_LINUX_PROOF): $(ORLIXOS_UTIL_LINUX_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	ldflags="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o"; \
	libs="-Wl,--start-group $$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build util-linux package inputs; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to package util-linux mount; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to package util-linux mount; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package util-linux package inputs; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" "$(ORLIXOS_SETSID_BINARY)" "$(ORLIXOS_MOUNT_BINARY)" "$(ORLIXOS_UMOUNT_BINARY)" "$(ORLIXOS_UTIL_LINUX_PROOF)"; \
	mkdir -p "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" "$(dir $(ORLIXOS_SETSID_BINARY))"; \
	cd "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fPIC"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -include signal.h"; \
	export LDFLAGS="$$ldflags"; \
	export LIBS="$$libs"; \
	"$(ORLIXOS_UTIL_LINUX_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --without-python --without-systemd --without-systemdsystemunitdir --without-cap-ng --without-libz --without-libmagic --without-udev --disable-nls --enable-static --disable-shared --enable-static-programs=mount,umount; \
	$(MAKE) -C "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" V=0 mount umount -j1 LDFLAGS="$$ldflags" LIBS="$$libs"; \
	cp "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)/mount" "$(ORLIXOS_MOUNT_BINARY)"; \
	cp "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)/umount" "$(ORLIXOS_UMOUNT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_MOUNT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_UMOUNT_BINARY)"; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -DHAVE_CONFIG_H -include "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)/config.h" -I"$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" -I"$(ORLIXOS_UTIL_LINUX_SRC_DIR)/include" -I"$(ORLIXOS_UTIL_LINUX_SRC_DIR)" $(ORLIXOS_PACKAGE_CFLAGS) -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_UTIL_LINUX_SRC_DIR)/sys-utils/setsid.c" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_SETSID_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_SETSID_BINARY)"; \
	file "$(ORLIXOS_SETSID_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SETSID_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=util-linux\nprograms=setsid,mount,umount\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(UTIL_LINUX_VERSION)" "$(UTIL_LINUX_SHA256)" > "$(ORLIXOS_UTIL_LINUX_PROOF)"; \
	rm -rf "$(ORLIXOS_UTIL_LINUX_BUILD_DIR)" "$(ORLIXOS_UTIL_LINUX_SRC_DIR)"; \
	echo "built Orlix Linux util-linux package inputs: $(ORLIXOS_SETSID_BINARY) $(ORLIXOS_MOUNT_BINARY) $(ORLIXOS_UMOUNT_BINARY)"

$(ORLIXOS_SETSID_BINARY): $(ORLIXOS_UTIL_LINUX_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_SETSID_BINARY)" ] || { echo "missing util-linux setsid package input: $(ORLIXOS_SETSID_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_SETSID_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SETSID_BINARY)" >&2; exit 1; }

$(ORLIXOS_MOUNT_BINARY): $(ORLIXOS_UTIL_LINUX_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_MOUNT_BINARY)" ] || { echo "missing util-linux mount package input: $(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_MOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_MOUNT_BINARY)" >&2; exit 1; }

$(ORLIXOS_UMOUNT_BINARY): $(ORLIXOS_UTIL_LINUX_PROOF)
	@set -euo pipefail; \
	[ -x "$(ORLIXOS_UMOUNT_BINARY)" ] || { echo "missing util-linux umount package input: $(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }; \
	file "$(ORLIXOS_UMOUNT_BINARY)" | grep -F -q 'statically linked' || { file "$(ORLIXOS_UMOUNT_BINARY)" >&2; exit 1; }

$(ORLIXOS_GETCONF_BINARY): $(ORLIXOS_GETCONF_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build getconf; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package getconf; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_GETCONF_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_GETCONF_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_GETCONF_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GETCONF_BINARY)"; \
	file "$(ORLIXOS_GETCONF_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETCONF_BINARY)" >&2; exit 1; }; \
	echo "built Orlix Linux getconf package input: $(ORLIXOS_GETCONF_BINARY)"

$(ORLIXOS_GETENT_BINARY): $(ORLIXOS_GETENT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_GETENT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_GETENT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_GETENT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GETENT_BINARY)"; \
	file "$(ORLIXOS_GETENT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GETENT_BINARY)" >&2; exit 1; }; \
	echo "built Orlix Linux getent package input: $(ORLIXOS_GETENT_BINARY)"

$(ORLIXOS_COREUTILS_TEST_INIT_BINARY): $(ORLIXOS_COREUTILS_TEST_INIT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_COREUTILS_TEST_INIT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -DORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS=$(ORLIXOS_COREUTILS_TEST_TIMEOUT_SECONDS) -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_COREUTILS_TEST_INIT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"; \
	file "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)" >&2; exit 1; }; \
	echo "built OrlixOS Coreutils test init: $(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"

$(ORLIXOS_COREUTILS_TEST_INITRAMFS): $(ORLIXOS_COREUTILS_TEST_INIT_BINARY) $(ORLIXOS_COREUTILS_TEST_RUNNER) $(ORLIXOS_COREUTILS_TEST_ENV) $(ORLIXOS_COREUTILS_SOURCE_STAMP) $(ORLIXOS_COREUTILS_TEST_LIST) $(ORLIXOS_COREUTILS_TEST_PASSWD) $(ORLIXOS_COREUTILS_TEST_GROUP) $(ORLIXOS_BASH_BINARY) $(ORLIXOS_COREUTILS_PROOF) $(ORLIXOS_GREP_BINARY) $(ORLIXOS_SED_BINARY) $(ORLIXOS_DIFF_BINARY) $(ORLIXOS_GAWK_BINARY) $(ORLIXOS_FINDUTILS_PROOF) $(ORLIXOS_SETSID_BINARY) $(ORLIXOS_MOUNT_BINARY) $(ORLIXOS_UMOUNT_BINARY) $(ORLIXOS_GETLIMITS_BINARY) $(ORLIXOS_GETCONF_BINARY) $(ORLIXOS_GETENT_BINARY) $(ORLIXOS_PERL_PROOF)
	@set -euo pipefail; \
	$(KERNEL_MAKE) prepare PROFILE="$(PROFILE)" >/dev/null; \
	gen_init_cpio="$(REPO_ROOT)/Build/OrlixKernel/build/$(PROFILE)/usr/gen_init_cpio"; \
	output="$(ORLIXOS_COREUTILS_TEST_INITRAMFS_DIR)"; \
	cpio_list="$(ORLIXOS_COREUTILS_TEST_INITRAMFS_DIR)/initramfs.list"; \
	case "$$output" in "$(REPO_ROOT)"/Build/OrlixOS/test-initramfs/*) ;; *) echo "refusing to write Coreutils test initramfs outside Build/OrlixOS/test-initramfs: $$output" >&2; exit 1 ;; esac; \
	[ -x "$$gen_init_cpio" ] || { echo "missing Linux gen_init_cpio: $$gen_init_cpio" >&2; exit 1; }; \
	rm -rf "$$output"; \
	mkdir -p "$$output/rootfs"; \
	{ \
		printf 'dir /bin 0755 0 0\n'; \
		printf 'dir /etc 0755 0 0\n'; \
		printf 'dir /usr 0755 0 0\n'; \
		printf 'dir /usr/bin 0755 0 0\n'; \
		printf 'dir /dev 0755 0 0\n'; \
		printf 'nod /dev/console 0600 0 0 c 5 1\n'; \
		printf 'dir /proc 0555 0 0\n'; \
		printf 'dir /sys 0555 0 0\n'; \
		printf 'dir /root 0700 0 0\n'; \
			printf 'dir /tmp 1777 0 0\n'; \
			printf 'dir /coreutils 0755 0 0\n'; \
			printf 'dir /coreutils-build 0777 0 0\n'; \
			printf 'slink /coreutils-build/coreutils /coreutils 0777 0 0\n'; \
			printf 'file /init %s 0755 0 0\n' "$(ORLIXOS_COREUTILS_TEST_INIT_BINARY)"; \
		printf 'file /coreutils-build/run-upstream-coreutils-tests.sh %s 0755 0 0\n' "$(ORLIXOS_COREUTILS_TEST_RUNNER)"; \
		printf 'file /coreutils-build/coreutils-test-env.sh %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_ENV)"; \
		printf 'file /etc/passwd %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_PASSWD)"; \
		printf 'file /etc/group %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_GROUP)"; \
		printf 'file /coreutils-test-list.txt %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_TEST_LIST)"; \
		printf 'file /bin/bash %s 0755 0 0\n' "$(ORLIXOS_BASH_BINARY)"; \
		printf 'slink /bin/sh bash 0777 0 0\n'; \
		for program in $(ORLIXOS_COREUTILS_PROGRAMS); do printf 'file /bin/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		printf 'file /bin/grep %s 0755 0 0\n' "$(ORLIXOS_GREP_BINARY)"; \
		printf 'file /bin/sed %s 0755 0 0\n' "$(ORLIXOS_SED_BINARY)"; \
		printf 'file /bin/gawk %s 0755 0 0\n' "$(ORLIXOS_GAWK_BINARY)"; \
		printf 'slink /bin/awk gawk 0777 0 0\n'; \
		for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do printf 'file /bin/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do printf 'file /bin/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		printf 'file /bin/setsid %s 0755 0 0\n' "$(ORLIXOS_SETSID_BINARY)"; \
		printf 'file /bin/mount %s 0755 0 0\n' "$(ORLIXOS_MOUNT_BINARY)"; \
		printf 'file /bin/umount %s 0755 0 0\n' "$(ORLIXOS_UMOUNT_BINARY)"; \
			printf 'file /bin/getlimits %s 0755 0 0\n' "$(ORLIXOS_GETLIMITS_BINARY)"; \
			printf 'file /bin/getconf %s 0755 0 0\n' "$(ORLIXOS_GETCONF_BINARY)"; \
			printf 'file /bin/getent %s 0755 0 0\n' "$(ORLIXOS_GETENT_BINARY)"; \
		printf 'slink /bin/perl /usr/bin/perl 0777 0 0\n'; \
		printf 'slink /bin/egrep grep 0777 0 0\n'; \
		printf 'slink /usr/bin/bash /bin/bash 0777 0 0\n'; \
		printf 'slink /usr/bin/sh /bin/sh 0777 0 0\n'; \
		printf 'slink /usr/bin/grep /bin/grep 0777 0 0\n'; \
		printf 'slink /usr/bin/sed /bin/sed 0777 0 0\n'; \
		printf 'slink /usr/bin/gawk /bin/gawk 0777 0 0\n'; \
		printf 'slink /usr/bin/awk /bin/awk 0777 0 0\n'; \
		for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do printf 'slink /usr/bin/%s /bin/%s 0777 0 0\n' "$$program" "$$program"; done; \
		for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do printf 'slink /usr/bin/%s /bin/%s 0777 0 0\n' "$$program" "$$program"; done; \
		printf 'slink /usr/bin/setsid /bin/setsid 0777 0 0\n'; \
		printf 'slink /usr/bin/mount /bin/mount 0777 0 0\n'; \
		printf 'slink /usr/bin/umount /bin/umount 0777 0 0\n'; \
			printf 'slink /usr/bin/getlimits /bin/getlimits 0777 0 0\n'; \
			printf 'slink /usr/bin/getconf /bin/getconf 0777 0 0\n'; \
			printf 'slink /usr/bin/getent /bin/getent 0777 0 0\n'; \
		printf 'file /usr/bin/perl %s 0755 0 0\n' "$(ORLIXOS_PERL_BINARY)"; \
		printf 'dir /usr/lib 0755 0 0\n'; \
		printf 'dir /usr/lib/perl5 0755 0 0\n'; \
		find "$(ORLIXOS_PERL_LIB_DIR)" -type d | sort | while read -r dir; do \
			rel="$${dir#"$(ORLIXOS_PACKAGE_INSTALL_DIR)"}"; \
			printf 'dir %s 0755 0 0\n' "$$rel"; \
		done; \
		find "$(ORLIXOS_PERL_LIB_DIR)" -type f | sort | while read -r file; do \
			rel="$${file#"$(ORLIXOS_PACKAGE_INSTALL_DIR)"}"; \
			printf 'file %s %s 0644 0 0\n' "$$rel" "$$file"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/tests" -type d | sort | while read -r dir; do \
			rel="$${dir#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			[ -n "$$rel" ] || rel=""; \
			printf 'dir /coreutils%s 0755 0 0\n' "$$rel"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/tests" -type f | sort | while read -r file; do \
			rel="$${file#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			printf 'file /coreutils%s %s 0644 0 0\n' "$$rel" "$$file"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/build-aux" -type d | sort | while read -r dir; do \
			rel="$${dir#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			printf 'dir /coreutils%s 0755 0 0\n' "$$rel"; \
		done; \
		find "$(ORLIXOS_COREUTILS_SRC_DIR)/build-aux" -type f | sort | while read -r file; do \
			rel="$${file#"$(ORLIXOS_COREUTILS_SRC_DIR)"}"; \
			mode=0644; \
			[ -x "$$file" ] && mode=0755; \
			printf 'file /coreutils%s %s %s 0 0\n' "$$rel" "$$file" "$$mode"; \
		done; \
		printf 'file /coreutils/init.cfg %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_SRC_DIR)/init.cfg"; \
		printf 'dir /coreutils-build/src 0755 0 0\n'; \
		for program in $(ORLIXOS_COREUTILS_PROGRAMS); do printf 'file /coreutils-build/src/%s %s 0755 0 0\n' "$$program" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
		printf 'dir /coreutils-build/lib 0755 0 0\n'; \
		if [ -s "$(ORLIXOS_COREUTILS_CONFIG_HEADER)" ]; then printf 'file /coreutils-build/lib/config.h %s 0644 0 0\n' "$(ORLIXOS_COREUTILS_CONFIG_HEADER)"; fi; \
	} > "$$cpio_list"; \
	"$$gen_init_cpio" "$$cpio_list" > "$$output/rootfs/initramfs.cpio"; \
	gzip -n -f "$$output/rootfs/initramfs.cpio"; \
	[ -s "$$output/rootfs/initramfs.cpio.gz" ] || { echo "missing packaged Coreutils test initramfs: $$output/rootfs/initramfs.cpio.gz" >&2; exit 1; }; \
	echo "packaged upstream Coreutils test initramfs: $$output"

$(ORLIXOS_GREP_BINARY): $(ORLIXOS_GREP_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build grep; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build grep; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build grep; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package grep; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_GREP_BUILD_DIR)" "$(ORLIXOS_GREP_BINARY)"; \
	mkdir -p "$(ORLIXOS_GREP_BUILD_DIR)" "$(dir $(ORLIXOS_GREP_BINARY))"; \
	cd "$(ORLIXOS_GREP_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	"$(ORLIXOS_GREP_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-perl-regexp --disable-gcc-warnings; \
	$(MAKE) -C lib -j1 all; \
	$(MAKE) -C src -j1 grep; \
	cp "$(ORLIXOS_GREP_BUILD_DIR)/src/grep" "$(ORLIXOS_GREP_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GREP_BINARY)"; \
	file "$(ORLIXOS_GREP_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GREP_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=grep\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(GREP_VERSION)" "$(GREP_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/grep.proof"; \
	rm -rf "$(ORLIXOS_GREP_BUILD_DIR)" "$(ORLIXOS_GREP_SRC_DIR)"; \
	echo "built Orlix Linux grep package input: $(ORLIXOS_GREP_BINARY)"

$(ORLIXOS_SED_BINARY): $(ORLIXOS_SED_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build sed; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build sed; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build sed; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package sed; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_SED_BUILD_DIR)" "$(ORLIXOS_SED_BINARY)"; \
	mkdir -p "$(ORLIXOS_SED_BUILD_DIR)" "$(dir $(ORLIXOS_SED_BINARY))"; \
	cd "$(ORLIXOS_SED_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export gl_cv_func_mbrtowc_incomplete_state=yes; \
	export gl_cv_func_mbrtowc_sanitycheck=yes; \
	export gl_cv_func_mbrtowc_null_arg1=yes; \
	export gl_cv_func_mbrtowc_null_arg2=yes; \
	export gl_cv_func_mbrtowc_retval=yes; \
	export gl_cv_func_mbrtowc_nul_retval=yes; \
	export gl_cv_func_mbrtowc_stores_incomplete=no; \
	export gl_cv_func_mbrtowc_empty_input=yes; \
	export gl_cv_func_mbrtowc_C_locale_sans_EILSEQ=no; \
	"$(ORLIXOS_SED_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-acl --disable-gcc-warnings; \
	$(MAKE) -j1 all; \
	$(MAKE) -j1 install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_SED_BINARY)"; \
	file "$(ORLIXOS_SED_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_SED_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=sed\nversion=%s\nsha256=%s\nregex=bundled-gnulib\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(SED_VERSION)" "$(SED_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/sed.proof"; \
	if [ "$(ORLIXOS_KEEP_SED_BUILD)" != "1" ]; then rm -rf "$(ORLIXOS_SED_BUILD_DIR)" "$(ORLIXOS_SED_SRC_DIR)"; fi; \
	echo "built Orlix Linux sed package input: $(ORLIXOS_SED_BINARY)"

$(ORLIXOS_DIFF_BINARY): $(ORLIXOS_DIFFUTILS_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build diffutils; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build diffutils; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build diffutils; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package diffutils; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_DIFFUTILS_BUILD_DIR)"; \
	for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do rm -f "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; done; \
	mkdir -p "$(ORLIXOS_DIFFUTILS_BUILD_DIR)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin"; \
	cd "$(ORLIXOS_DIFFUTILS_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export gl_cv_func_getopt_gnu=yes; \
	export gl_cv_func_getopt_long_gnu=yes; \
	export gl_cv_func_strcasecmp_works=yes; \
	"$(ORLIXOS_DIFFUTILS_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-gcc-warnings; \
	$(MAKE) -j1 all; \
	$(MAKE) -j1 install DESTDIR="$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	for program in $(ORLIXOS_DIFFUTILS_PROGRAMS); do \
		"$(ORLIXOS_STRIP)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program"; \
		file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" >&2; exit 1; }; \
	done; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=diffutils\nversion=%s\nsha256=%s\nprograms=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(DIFFUTILS_VERSION)" "$(DIFFUTILS_SHA256)" "$(ORLIXOS_DIFFUTILS_PROGRAMS)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/diffutils.proof"; \
	rm -rf "$(ORLIXOS_DIFFUTILS_BUILD_DIR)" "$(ORLIXOS_DIFFUTILS_SRC_DIR)"; \
	echo "built Orlix Linux diffutils package inputs: $(ORLIXOS_DIFFUTILS_PROGRAMS)"

$(ORLIXOS_GAWK_BINARY): $(ORLIXOS_GAWK_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build gawk; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build gawk; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build gawk; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package gawk; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_GAWK_BUILD_DIR)" "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)" "$(ORLIXOS_GAWK_BINARY)"; \
	mkdir -p "$(ORLIXOS_GAWK_BUILD_DIR)" "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)" "$(dir $(ORLIXOS_GAWK_BINARY))"; \
	{ \
		printf '%s\n' '#!/bin/bash'; \
		printf '%s\n' 'set -euo pipefail'; \
		printf '%s\n' 'cc="$(ORLIXOS_CC)"'; \
		printf '%s\n' 'sysroot="$(ORLIXOS_MLIBC_SYSROOT)"'; \
		printf '%s\n' 'headers="$(ORLIXOS_MLIBC_HEADERS)"'; \
		printf '%s\n' 'rtlib="$(ORLIXOS_MLIBC_RTLIB)"'; \
		printf '%s\n' 'link=1'; \
		printf '%s\n' 'for arg in "$$@"; do'; \
		printf '%s\n' '  case "$$arg" in -c|-E|-S) link=0 ;; esac'; \
		printf '%s\n' 'done'; \
		printf '%s\n' 'common=(--target=aarch64-linux-gnu "--sysroot=$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie)'; \
		printf '%s\n' 'if [ "$$link" -eq 1 ]; then'; \
		printf '%s\n' '  exec "$$cc" "$${common[@]}" "$$@" -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o"'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'exec "$$cc" "$${common[@]}" "$$@"'; \
	} > "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	chmod +x "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	cd "$(ORLIXOS_GAWK_BUILD_DIR)"; \
	export CC="$(ORLIXOS_GAWK_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS) -D__KLIBC__"; \
	export LDFLAGS=""; \
	export LIBS=""; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	"$(ORLIXOS_GAWK_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-nls --disable-mpfr --without-readline; \
	$(MAKE) -j1 all; \
	cp "$(ORLIXOS_GAWK_BUILD_DIR)/gawk" "$(ORLIXOS_GAWK_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_GAWK_BINARY)"; \
	file "$(ORLIXOS_GAWK_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_GAWK_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=gawk\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(GAWK_VERSION)" "$(GAWK_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/gawk.proof"; \
	rm -rf "$(ORLIXOS_GAWK_BUILD_DIR)" "$(ORLIXOS_GAWK_TOOLCHAIN_DIR)" "$(ORLIXOS_GAWK_SRC_DIR)"; \
	echo "built Orlix Linux gawk package input: $(ORLIXOS_GAWK_BINARY)"

$(ORLIXOS_PERL_PROOF): $(ORLIXOS_PERL_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build perl; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build perl; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build perl; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_NM)" >/dev/null 2>&1 || { echo "llvm-nm is required to build perl; set ORLIXOS_NM=/path/to/llvm-nm" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package perl; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	command -v "$(ORLIXOS_READELF)" >/dev/null 2>&1 || { echo "llvm-readelf is required to build perl; set ORLIXOS_READELF=/path/to/llvm-readelf" >&2; exit 1; }; \
	command -v "$(ORLIXOS_OBJDUMP)" >/dev/null 2>&1 || { echo "llvm-objdump is required to build perl; set ORLIXOS_OBJDUMP=/path/to/llvm-objdump" >&2; exit 1; }; \
	command -v gsed >/dev/null 2>&1 || { echo "GNU sed is required to configure perl; install gsed or put it on PATH" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_PERL_TOOLCHAIN_DIR)" "$(ORLIXOS_PERL_BINARY)" "$(ORLIXOS_PERL_LIB_DIR)" "$(ORLIXOS_PERL_PROOF)"; \
	mkdir -p "$(ORLIXOS_PERL_TOOLCHAIN_DIR)" "$(dir $(ORLIXOS_PERL_BINARY))" "$(dir $(ORLIXOS_PERL_LIB_DIR))"; \
	{ \
		printf '%s\n' '#!/bin/bash'; \
		printf '%s\n' 'set -euo pipefail'; \
		printf '%s\n' 'cc="$(ORLIXOS_CC)"'; \
		printf '%s\n' 'sysroot="$(ORLIXOS_MLIBC_SYSROOT)"'; \
		printf '%s\n' 'headers="$(ORLIXOS_MLIBC_HEADERS)"'; \
		printf '%s\n' 'rtlib="$(ORLIXOS_MLIBC_RTLIB)"'; \
		printf '%s\n' 'link=1'; \
		printf '%s\n' 'for arg in "$$@"; do'; \
		printf '%s\n' '  case "$$arg" in -c|-E|-S) link=0 ;; esac'; \
		printf '%s\n' 'done'; \
		printf '%s\n' 'common=(--target=aarch64-linux-gnu "--sysroot=$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie)'; \
		printf '%s\n' 'if [ "$$link" -eq 1 ]; then'; \
		printf '%s\n' '  exec "$$cc" "$${common[@]}" "$$@" -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o"'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'exec "$$cc" "$${common[@]}" "$$@"'; \
	} > "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	chmod +x "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-gcc"; \
	ln -sf "$(ORLIXOS_AR)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-ar"; \
	ln -sf "$(ORLIXOS_RANLIB)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-ranlib"; \
	ln -sf "$(ORLIXOS_NM)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-nm"; \
	ln -sf "$(ORLIXOS_OBJDUMP)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/aarch64-linux-gnu-objdump"; \
	ln -sf "$(ORLIXOS_READELF)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/readelf"; \
	ln -sf "$$(command -v gsed)" "$(ORLIXOS_PERL_TOOLCHAIN_DIR)/sed"; \
	cd "$(ORLIXOS_PERL_SRC_DIR)"; \
	PATH="$(ORLIXOS_PERL_TOOLCHAIN_DIR):$$PATH" READELF=readelf ./configure --target=aarch64-linux-gnu --prefix=/usr --sysroot="$$sysroot" --target-tools-prefix=aarch64-linux-gnu- --no-dynaloader --only-mod=Errno,Fcntl,File-Glob,IO --host-cc="$(ORLIXOS_CC)" --host-set=d_nanosleep=define -Ud_syscall -Ud_syscallproto -Dcharsize=1 -Dshortsize=2 -Dintsize=4 -Dlongsize=8 -Ddoublesize=8 -Dptrsize=8 -Dlongdblsize=16 -Dlonglongsize=8; \
	perl -0pi -e 's/^# HAS_NANOSLEEP/#define HAS_NANOSLEEP/m' xconfig.h; \
	PATH="$(ORLIXOS_PERL_TOOLCHAIN_DIR):$$PATH" READELF=readelf $(MAKE) -j1 perl; \
	cp "$(ORLIXOS_PERL_SRC_DIR)/perl" "$(ORLIXOS_PERL_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_PERL_BINARY)"; \
	cp -Rf "$(ORLIXOS_PERL_SRC_DIR)/lib/." "$(ORLIXOS_PERL_LIB_DIR)/"; \
	for module_lib in "$(ORLIXOS_PERL_SRC_DIR)"/cpan/*/lib "$(ORLIXOS_PERL_SRC_DIR)"/dist/*/lib "$(ORLIXOS_PERL_SRC_DIR)"/ext/*/lib; do \
		[ -d "$$module_lib" ] || continue; \
		cp -Rf "$$module_lib/." "$(ORLIXOS_PERL_LIB_DIR)/"; \
	done; \
	chmod -R u+w "$(ORLIXOS_PERL_LIB_DIR)"; \
	for module_pm in "$(ORLIXOS_PERL_SRC_DIR)"/cpan/*/*.pm "$(ORLIXOS_PERL_SRC_DIR)"/dist/*/*.pm "$(ORLIXOS_PERL_SRC_DIR)"/ext/*/*.pm; do \
		[ -f "$$module_pm" ] || continue; \
		[ -f "$$(dirname "$$module_pm")/Makefile" ] || continue; \
		module_path="$$(perl -ne 'if (/^package[[:space:]]+([A-Za-z0-9_:]+)[[:space:]]*;/) { $$m=$$1; $$m =~ s!::!/!g; print "$$m.pm"; exit }' "$$module_pm")"; \
		[ -n "$$module_path" ] || continue; \
		mkdir -p "$(ORLIXOS_PERL_LIB_DIR)/$$(dirname "$$module_path")"; \
		rm -f "$(ORLIXOS_PERL_LIB_DIR)/$$module_path"; \
		cp "$$module_pm" "$(ORLIXOS_PERL_LIB_DIR)/$$module_path"; \
	done; \
	if [ -f "$(ORLIXOS_PERL_SRC_DIR)/dist/IO/IO.pm" ]; then rm -f "$(ORLIXOS_PERL_LIB_DIR)/IO.pm"; cp "$(ORLIXOS_PERL_SRC_DIR)/dist/IO/IO.pm" "$(ORLIXOS_PERL_LIB_DIR)/IO.pm"; fi; \
	cd "$(ORLIXOS_PERL_SRC_DIR)/dist/XSLoader"; ../../miniperl_top -I../../lib XSLoader_pm.PL; rm -f "$(ORLIXOS_PERL_LIB_DIR)/XSLoader.pm"; cp XSLoader.pm "$(ORLIXOS_PERL_LIB_DIR)/XSLoader.pm"; \
	cd "$(ORLIXOS_PERL_SRC_DIR)/ext/DynaLoader"; ../../miniperl_top -I../../lib DynaLoader_pm.PL; rm -f "$(ORLIXOS_PERL_LIB_DIR)/DynaLoader.pm"; cp DynaLoader.pm "$(ORLIXOS_PERL_LIB_DIR)/DynaLoader.pm"; \
	file "$(ORLIXOS_PERL_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_PERL_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=perl\nversion=%s\nsha256=%s\nperl_cross_version=%s\nperl_cross_sha256=%s\nstatic_modules=Errno,Fcntl,File-Glob,IO\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(PERL_VERSION)" "$(PERL_SHA256)" "$(PERL_CROSS_VERSION)" "$(PERL_CROSS_SHA256)" > "$(ORLIXOS_PERL_PROOF)"; \
	rm -rf "$(ORLIXOS_PERL_TOOLCHAIN_DIR)" "$(ORLIXOS_PERL_SRC_DIR)"; \
	echo "built Orlix Linux perl package input: $(ORLIXOS_PERL_BINARY)"

$(ORLIXOS_JQ_BINARY): $(ORLIXOS_JQ_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build jq; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build jq; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build jq; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package jq; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_JQ_BUILD_DIR)" "$(ORLIXOS_JQ_BINARY)"; \
	mkdir -p "$(dir $(ORLIXOS_JQ_BUILD_DIR))" "$(dir $(ORLIXOS_JQ_BINARY))"; \
	cp -R "$(ORLIXOS_JQ_SRC_DIR)" "$(ORLIXOS_JQ_BUILD_DIR)"; \
	cd "$(ORLIXOS_JQ_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export CPPFLAGS="-Imodules/oniguruma/src"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	orlixos_package_libs="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export LIBS="$$orlixos_package_libs"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export ac_cv_func_isatty=yes; \
	export ac_cv_func_setlocale=yes; \
	export ac_cv_func_strptime=yes; \
	export ac_cv_func_pthread_key_create=yes; \
	export ac_cv_func_pthread_once=yes; \
	./configure --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-shared --enable-static --enable-all-static --disable-docs --with-oniguruma=builtin; \
	$(MAKE) -C modules/oniguruma -j1 LIBS=; \
	$(MAKE) -j1 src/builtin.inc libjq.la LIBS=; \
	$(MAKE) -j1 jq LIBS="$$orlixos_package_libs"; \
	cp "$(ORLIXOS_JQ_BUILD_DIR)/jq" "$(ORLIXOS_JQ_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_JQ_BINARY)"; \
	file "$(ORLIXOS_JQ_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_JQ_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=jq\nversion=%s\nsha256=%s\nregex=oniguruma-builtin\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(JQ_VERSION)" "$(JQ_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/jq.proof"; \
	echo "built Orlix Linux jq package input: $(ORLIXOS_JQ_BINARY)"

$(ORLIXOS_CURL_BINARY): $(ORLIXOS_CURL_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build curl; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build curl; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build curl; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package curl; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_CURL_BUILD_DIR)" "$(ORLIXOS_CURL_BINARY)"; \
	mkdir -p "$(ORLIXOS_CURL_BUILD_DIR)" "$(dir $(ORLIXOS_CURL_BINARY))"; \
	cd "$(ORLIXOS_CURL_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export CPPFLAGS="-I$(ORLIXOS_CURL_SRC_DIR)/include"; \
	rtlib_dir="$$(dirname "$$rtlib")"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group -L$$sysroot/usr/lib -L$$rtlib_dir"; \
	export LIBS="-lc -lm -lpthread -lssp_nonshared -lssp -lorlix_compiler_rt -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export ac_cv_func_getpwuid_r=yes; \
	export ac_cv_func_geteuid=yes; \
	export ac_cv_func_getppid=yes; \
	export ac_cv_func_setlocale=yes; \
	"$(ORLIXOS_CURL_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-shared --enable-static --disable-docs --disable-manual --disable-threaded-resolver --disable-ldap --disable-ldaps --without-ssl --without-zlib --without-brotli --without-zstd --without-libidn2 --without-nghttp2 --without-ngtcp2 --without-nghttp3 --without-libpsl; \
	$(MAKE) -C lib -j1 libcurl.la; \
	$(MAKE) -C src -j1 curl; \
	cp "$(ORLIXOS_CURL_BUILD_DIR)/src/curl" "$(ORLIXOS_CURL_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_CURL_BINARY)"; \
	file "$(ORLIXOS_CURL_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_CURL_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=curl\nversion=%s\nsha256=%s\nfeatures=static-no-external-tls\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(CURL_VERSION)" "$(CURL_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/curl.proof"; \
	echo "built Orlix Linux curl package input: $(ORLIXOS_CURL_BINARY)"

$(ORLIXOS_NCURSES_LIBTINFO): $(ORLIXOS_NCURSES_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build ncurses; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build ncurses; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build ncurses; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_NCURSES_BUILD_DIR)" "$(ORLIXOS_NCURSES_SYSROOT)"; \
	mkdir -p "$(ORLIXOS_NCURSES_BUILD_DIR)" "$(ORLIXOS_NCURSES_SYSROOT)" "$(ORLIXOS_PACKAGE_INSTALL_DIR)"; \
	cd "$(ORLIXOS_NCURSES_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group -L$$sysroot/usr/lib -L$$(dirname "$$rtlib")"; \
	export LIBS="-lc -lm -lpthread -lssp_nonshared -lssp -lorlix_compiler_rt -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	"$(ORLIXOS_NCURSES_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --with-normal --without-shared --without-debug --without-ada --without-cxx --without-cxx-binding --without-progs --without-tests --without-manpages --with-termlib=tinfo --with-ticlib=tic --disable-db-install --disable-home-terminfo --disable-stripping; \
	$(MAKE) -j1 libs; \
	$(MAKE) -j1 DESTDIR="$(ORLIXOS_NCURSES_SYSROOT)" install.libs install.includes; \
	[ -s "$(ORLIXOS_NCURSES_LIBTINFO)" ] || { echo "missing ncurses terminfo archive: $(ORLIXOS_NCURSES_LIBTINFO)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_NCURSES_LIBNCURSES)" ] || { echo "missing ncurses archive: $(ORLIXOS_NCURSES_LIBNCURSES)" >&2; exit 1; }; \
	"$(ORLIXOS_AR)" t "$(ORLIXOS_NCURSES_LIBTINFO)" >/dev/null; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=ncurses\nversion=%s\nsha256=%s\nfeatures=static-zsh-build-dependency\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(NCURSES_VERSION)" "$(NCURSES_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/ncurses.proof"; \
	echo "built Orlix Linux ncurses static package input: $(ORLIXOS_NCURSES_LIBTINFO)"

$(ORLIXOS_ZSH_BINARY): $(ORLIXOS_ZSH_SOURCE_STAMP) $(ORLIXOS_NCURSES_LIBTINFO) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_NCURSES_LIBTINFO)" ] || { echo "missing ncurses terminfo archive: $(ORLIXOS_NCURSES_LIBTINFO)" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_NCURSES_LIBNCURSES)" ] || { echo "missing ncurses archive: $(ORLIXOS_NCURSES_LIBNCURSES)" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build zsh; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package zsh; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	command -v "$(ORLIXOS_READELF)" >/dev/null 2>&1 || { echo "llvm-readelf is required to verify zsh; set ORLIXOS_READELF=/path/to/llvm-readelf" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_ZSH_BUILD_DIR)" "$(ORLIXOS_ZSH_BINARY)"; \
	mkdir -p "$(ORLIXOS_ZSH_BUILD_DIR)" "$(dir $(ORLIXOS_ZSH_BINARY))"; \
	cd "$(ORLIXOS_ZSH_BUILD_DIR)"; \
	rtlib_dir="$$(dirname "$$rtlib")"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="$(ORLIXOS_PACKAGE_CFLAGS)"; \
	export CPPFLAGS="-I$(ORLIXOS_NCURSES_SYSROOT)/usr/include"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -no-pie -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--no-dynamic-linker -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group -L$(ORLIXOS_NCURSES_SYSROOT)/usr/lib -L$$sysroot/usr/lib -L$$rtlib_dir"; \
	export LIBS="-ltinfo -lc -lm -lpthread -lssp_nonshared -lssp -lorlix_compiler_rt -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export STRIP="$(ORLIXOS_STRIP)"; \
	export zsh_cv_sys_dynamic_clash_ok=yes; \
	export zsh_cv_sys_dynamic_execsyms=no; \
	export zsh_cv_shared_environ=yes; \
	"$(ORLIXOS_ZSH_SRC_DIR)/configure" --host=aarch64-linux-gnu --build=aarch64-apple-darwin --prefix=/usr --disable-dynamic --disable-gdbm --disable-pcre --disable-cap --disable-dynamic-nss --with-term-lib=tinfo --enable-etcdir=/etc/zsh --enable-zshenv=/etc/zsh/zshenv --enable-zshrc=/etc/zsh/zshrc --enable-zprofile=/etc/zsh/zprofile --enable-zlogin=/etc/zsh/zlogin --enable-zlogout=/etc/zsh/zlogout; \
	$(MAKE) -C Src -j1 zsh; \
	cp "$(ORLIXOS_ZSH_BUILD_DIR)/Src/zsh" "$(ORLIXOS_ZSH_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_ZSH_BINARY)"; \
	file "$(ORLIXOS_ZSH_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_ZSH_BINARY)" >&2; exit 1; }; \
	if "$(ORLIXOS_READELF)" -l "$(ORLIXOS_ZSH_BINARY)" | grep -F -q 'INTERP'; then echo "zsh must be a static Orlix Linux ELF without PT_INTERP" >&2; exit 1; fi; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\npackage=zsh\nversion=%s\nsha256=%s\nterminal_library=ncurses-%s-static\nfeatures=static-no-dynamic-modules\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ZSH_VERSION)" "$(ZSH_SHA256)" "$(NCURSES_VERSION)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/zsh.proof"; \
	echo "built Orlix Linux zsh package input: $(ORLIXOS_ZSH_BINARY)"

$(ORLIXOS_INIT_BINARY): $(ORLIXOS_INIT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Orlix init; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Orlix init; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_INIT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_INIT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_INIT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_INIT_BINARY)"; \
	file "$(ORLIXOS_INIT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_INIT_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\nprogram=init\nconsole=/dev/hvc0\nshell=/bin/sh\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/init.proof"; \
	echo "built OrlixOS first-stage init: $(ORLIXOS_INIT_BINARY)"

$(ORLIXOS_ROOT_INIT_BINARY): $(ORLIXOS_ROOT_INIT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Orlix root init; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Orlix root init; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_ROOT_INIT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_ROOT_INIT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_ROOT_INIT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_ROOT_INIT_BINARY)"; \
	file "$(ORLIXOS_ROOT_INIT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_ROOT_INIT_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\nprogram=rootinit\nroot_mode=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_PROFILE_ROOT_MODE)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/rootinit.proof"; \
	echo "built OrlixOS root initramfs init: $(ORLIXOS_ROOT_INIT_BINARY)"

$(ORLIXOS_INITRAMFS_CPIO): $(ORLIXOS_ROOT_INIT_BINARY) $(ORLIXOS_MANIFEST)
	@set -euo pipefail; \
	$(KERNEL_MAKE) prepare PROFILE="$(PROFILE)" >/dev/null; \
	gen_init_cpio="$(REPO_ROOT)/Build/OrlixKernel/build/$(PROFILE)/usr/gen_init_cpio"; \
	output="$(ORLIXOS_INITRAMFS_CPIO)"; \
	cpio_list="$(ORLIXOS_ROOTFS_DIR)/initramfs.list"; \
	case "$$output" in "$(REPO_ROOT)"/Build/OrlixOS/rootfs/*/rootfs/initramfs.cpio.gz) ;; *) echo "refusing to write OrlixOS initramfs outside Build/OrlixOS/rootfs: $$output" >&2; exit 1 ;; esac; \
	[ -x "$$gen_init_cpio" ] || { echo "missing Linux gen_init_cpio: $$gen_init_cpio" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_INITRAMFS_DIR)"; \
	{ \
		printf 'dir /dev 0755 0 0\n'; \
		printf 'nod /dev/console 0600 0 0 c 5 1\n'; \
		printf 'dir /root 0700 0 0\n'; \
		if [ "$(ORLIXOS_PROFILE_ROOT_MODE)" = overlay ]; then \
			printf 'file /init %s 0755 0 0\n' "$(ORLIXOS_ROOT_INIT_BINARY)"; \
		fi; \
	} > "$$cpio_list"; \
	"$$gen_init_cpio" "$$cpio_list" > "$$output.tmp"; \
	gzip -n -c "$$output.tmp" > "$$output"; \
	rm -f "$$output.tmp"; \
	[ -s "$$output" ] || { echo "missing generated OrlixOS initramfs: $$output" >&2; exit 1; }; \
	echo "built OrlixOS product initramfs: $$output"

$(ORLIXOS_ROOTFS_PROOF): $(ORLIXOS_BASH_BINARY) $(ORLIXOS_COREUTILS_PROOF) $(ORLIXOS_FINDUTILS_PROOF) $(ORLIXOS_GETCONF_BINARY) $(ORLIXOS_INIT_BINARY) $(ORLIXOS_INITRAMFS_CPIO) $(ORLIXOS_MANIFEST)
	@set -euo pipefail; \
	root_tree="$(ORLIXOS_BASE_ROOT_TREE)"; \
	state_tree="$(ORLIXOS_STATE_ROOT_TREE)"; \
	case "$$root_tree" in "$(REPO_ROOT)"/Build/OrlixOS/rootfs/*/base-tree) ;; *) echo "refusing to write OrlixOS root tree outside Build/OrlixOS/rootfs: $$root_tree" >&2; exit 1 ;; esac; \
	case "$$state_tree" in "$(REPO_ROOT)"/Build/OrlixOS/rootfs/*/state-tree) ;; *) echo "refusing to write OrlixOS state tree outside Build/OrlixOS/rootfs: $$state_tree" >&2; exit 1 ;; esac; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_ROOTFS_DIR)" "$$root_tree" "$$state_tree"; do \
		if [ -L "$$path" ]; then echo "refusing to package OrlixOS root tree through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$$root_tree" "$$state_tree"; \
	mkdir -p "$$root_tree/bin" "$$root_tree/dev" "$$root_tree/etc" "$$root_tree/proc" "$$root_tree/root" "$$root_tree/run" "$$root_tree/sbin" "$$root_tree/sys" "$$root_tree/tmp" "$$root_tree/usr/bin" "$$root_tree/usr/share/orlixos" "$$root_tree/var/tmp"; \
	mkdir -p "$$state_tree/upper" "$$state_tree/work"; \
	install -m 0755 "$(ORLIXOS_BASH_BINARY)" "$$root_tree/bin/bash"; \
	for program in $(ORLIXOS_COREUTILS_PROGRAMS); do install -m 0755 "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" "$$root_tree/bin/$$program"; done; \
	for program in $(ORLIXOS_FINDUTILS_PROGRAMS); do install -m 0755 "$(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/$$program" "$$root_tree/bin/$$program"; done; \
	install -m 0755 "$(ORLIXOS_GETCONF_BINARY)" "$$root_tree/usr/bin/getconf"; \
	install -m 0755 "$(ORLIXOS_INIT_BINARY)" "$$root_tree/sbin/init"; \
	ln -s bash "$$root_tree/bin/sh"; \
	printf '%s\n' 'root:x:0:0:root:/root:/bin/sh' > "$$root_tree/etc/passwd"; \
	printf '%s\n' 'root:x:0:root' > "$$root_tree/etc/group"; \
	printf '%s\n' 'NAME=$(ORLIXOS_DISTRIBUTION_NAME)' 'ID=$(ORLIXOS_DISTRIBUTION_ID)' 'PRETTY_NAME=$(ORLIXOS_DISTRIBUTION_NAME)' > "$$root_tree/etc/os-release"; \
	{ \
		printf 'distribution=%s\n' "$(ORLIXOS_DISTRIBUTION_ID)"; \
		printf 'profile=%s\n' "$(PROFILE)"; \
		printf 'channel=%s\n' "$(ORLIXOS_DISTRIBUTION_CHANNEL)"; \
		printf 'root_modes=%s\n' "$(ORLIXOS_ROOT_MODES)"; \
		printf 'selected_root_mode=%s\n' "$(ORLIXOS_PROFILE_ROOT_MODE)"; \
		printf 'base_root_device=%s\n' "$(ORLIXOS_BASE_ROOT_DEVICE)"; \
		printf 'state_root_device=%s\n' "$(ORLIXOS_STATE_ROOT_DEVICE)"; \
		printf 'packages=%s\n' "$(ORLIXOS_PROFILE_PACKAGES)"; \
		printf 'proof_ladder=%s\n' "$(ORLIXOS_PACKAGE_PROOF_LADDER)"; \
		printf 'downloaded_binary_repositories=%s\n' "$(ORLIXOS_DOWNLOADED_BINARY_REPOSITORIES)"; \
	} > "$$root_tree/usr/share/orlixos/distribution.manifest"; \
	chmod 0755 "$$root_tree" "$$root_tree/bin" "$$root_tree/dev" "$$root_tree/etc" "$$root_tree/proc" "$$root_tree/run" "$$root_tree/sbin" "$$root_tree/sys" "$$root_tree/usr" "$$root_tree/usr/bin" "$$root_tree/usr/share" "$$root_tree/usr/share/orlixos" "$$root_tree/var"; \
	chmod 0700 "$$root_tree/root"; \
	chmod 1777 "$$root_tree/tmp" "$$root_tree/var/tmp"; \
	chmod 0755 "$$state_tree" "$$state_tree/upper" "$$state_tree/work"; \
	printf 'profile=%s\ndistribution=%s\nchannel=%s\nroot_modes=%s\nselected_root_mode=%s\nbase_root_device=%s\nstate_root_device=%s\ninitramfs=%s\nbase_root_tree=%s\nstate_root_tree=%s\ninit=/sbin/init\nshell=/bin/sh\nbase_packages=bash coreutils findutils\ncoreutils_programs=%s\nfindutils_programs=%s\nbash_version=%s\ncoreutils_version=%s\nfindutils_version=%s\n' "$(PROFILE)" "$(ORLIXOS_DISTRIBUTION_ID)" "$(ORLIXOS_DISTRIBUTION_CHANNEL)" "$(ORLIXOS_ROOT_MODES)" "$(ORLIXOS_PROFILE_ROOT_MODE)" "$(ORLIXOS_BASE_ROOT_DEVICE)" "$(ORLIXOS_STATE_ROOT_DEVICE)" "$(ORLIXOS_INITRAMFS_CPIO)" "$$root_tree" "$$state_tree" "$(ORLIXOS_COREUTILS_PROGRAMS)" "$(ORLIXOS_FINDUTILS_PROGRAMS)" "$(BASH_VERSION)" "$(COREUTILS_VERSION)" "$(FINDUTILS_VERSION)" > "$(ORLIXOS_ROOTFS_PROOF)"; \
	echo "built OrlixOS base root tree: $$root_tree"

clean:
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)/build" "$(ORLIXOS_BUILD_ROOT)/packages" "$(ORLIXOS_BUILD_ROOT)/rootfs" "$(ORLIXOS_SRC_DIR)"

mrproper: clean
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)"
