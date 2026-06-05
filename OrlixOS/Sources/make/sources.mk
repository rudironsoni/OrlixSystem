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
	git -C "$(ORLIXOS_COREUTILS_SRC_DIR)" fetch --depth 1 origin "refs/tags/$(COREUTILS_GIT_REF):refs/tags/$(COREUTILS_GIT_REF)"; \
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

$(ORLIXOS_E2FSPROGS_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch e2fsprogs source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify e2fsprogs source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_E2FSPROGS_ARCHIVE)" ]; then curl -fL "$(E2FSPROGS_URL)" -o "$(ORLIXOS_E2FSPROGS_ARCHIVE)"; fi; \
	echo "upstream e2fsprogs archive ready: $(ORLIXOS_E2FSPROGS_ARCHIVE)"

$(ORLIXOS_E2FSPROGS_ARCHIVE_STAMP): $(ORLIXOS_E2FSPROGS_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify e2fsprogs source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(E2FSPROGS_SHA256)" "$(ORLIXOS_E2FSPROGS_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_E2FSPROGS_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_E2FSPROGS_ARCHIVE_STAMP)"; \
	echo "upstream e2fsprogs ready: $(ORLIXOS_E2FSPROGS_ARCHIVE)"

$(ORLIXOS_E2FSPROGS_SOURCE_STAMP): $(ORLIXOS_E2FSPROGS_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_E2FSPROGS_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_E2FSPROGS_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_E2FSPROGS_SOURCE_STAMP)"; \
	echo "extracted e2fsprogs source: $(ORLIXOS_E2FSPROGS_SRC_DIR)"

$(ORLIXOS_ATTR_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch attr source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify attr source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_ATTR_ARCHIVE)" ]; then curl -fL "$(ATTR_URL)" -o "$(ORLIXOS_ATTR_ARCHIVE)"; fi; \
	echo "upstream attr archive ready: $(ORLIXOS_ATTR_ARCHIVE)"

$(ORLIXOS_ATTR_ARCHIVE_STAMP): $(ORLIXOS_ATTR_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify attr source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(ATTR_SHA256)" "$(ORLIXOS_ATTR_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_ATTR_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_ATTR_ARCHIVE_STAMP)"; \
	echo "upstream attr ready: $(ORLIXOS_ATTR_ARCHIVE)"

$(ORLIXOS_ATTR_SOURCE_STAMP): $(ORLIXOS_ATTR_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_ATTR_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_ATTR_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_ATTR_SOURCE_STAMP)"; \
	echo "extracted attr source: $(ORLIXOS_ATTR_SRC_DIR)"

$(ORLIXOS_ACL_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch acl source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify acl source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_ACL_ARCHIVE)" ]; then curl -fL "$(ACL_URL)" -o "$(ORLIXOS_ACL_ARCHIVE)"; fi; \
	echo "upstream acl archive ready: $(ORLIXOS_ACL_ARCHIVE)"

$(ORLIXOS_ACL_ARCHIVE_STAMP): $(ORLIXOS_ACL_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify acl source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(ACL_SHA256)" "$(ORLIXOS_ACL_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_ACL_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_ACL_ARCHIVE_STAMP)"; \
	echo "upstream acl ready: $(ORLIXOS_ACL_ARCHIVE)"

$(ORLIXOS_ACL_SOURCE_STAMP): $(ORLIXOS_ACL_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_ACL_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xJf "$(ORLIXOS_ACL_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_ACL_SOURCE_STAMP)"; \
	echo "extracted acl source: $(ORLIXOS_ACL_SRC_DIR)"

$(ORLIXOS_PCRE2_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch pcre2 source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify pcre2 source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_PCRE2_ARCHIVE)" ]; then curl -fL "$(PCRE2_URL)" -o "$(ORLIXOS_PCRE2_ARCHIVE)"; fi; \
	echo "upstream pcre2 archive ready: $(ORLIXOS_PCRE2_ARCHIVE)"

$(ORLIXOS_PCRE2_ARCHIVE_STAMP): $(ORLIXOS_PCRE2_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify pcre2 source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(PCRE2_SHA256)" "$(ORLIXOS_PCRE2_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_PCRE2_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_PCRE2_ARCHIVE_STAMP)"; \
	echo "upstream pcre2 ready: $(ORLIXOS_PCRE2_ARCHIVE)"

$(ORLIXOS_PCRE2_SOURCE_STAMP): $(ORLIXOS_PCRE2_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_PCRE2_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xjf "$(ORLIXOS_PCRE2_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_PCRE2_SOURCE_STAMP)"; \
	echo "extracted pcre2 source: $(ORLIXOS_PCRE2_SRC_DIR)"

$(ORLIXOS_FTS_STANDALONE_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch musl-fts source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify musl-fts source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_FTS_STANDALONE_ARCHIVE)" ]; then curl -fL "$(FTS_STANDALONE_URL)" -o "$(ORLIXOS_FTS_STANDALONE_ARCHIVE)"; fi; \
	echo "upstream musl-fts archive ready: $(ORLIXOS_FTS_STANDALONE_ARCHIVE)"

$(ORLIXOS_FTS_STANDALONE_ARCHIVE_STAMP): $(ORLIXOS_FTS_STANDALONE_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify musl-fts source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(FTS_STANDALONE_SHA256)" "$(ORLIXOS_FTS_STANDALONE_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_FTS_STANDALONE_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_FTS_STANDALONE_ARCHIVE_STAMP)"; \
	echo "upstream musl-fts ready: $(ORLIXOS_FTS_STANDALONE_ARCHIVE)"

$(ORLIXOS_FTS_STANDALONE_SOURCE_STAMP): $(ORLIXOS_FTS_STANDALONE_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_FTS_STANDALONE_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_FTS_STANDALONE_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_FTS_STANDALONE_SOURCE_STAMP)"; \
	echo "extracted musl-fts source: $(ORLIXOS_FTS_STANDALONE_SRC_DIR)"

$(ORLIXOS_LIBSEPOL_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch libsepol source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify libsepol source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_LIBSEPOL_ARCHIVE)" ]; then curl -fL "$(LIBSEPOL_URL)" -o "$(ORLIXOS_LIBSEPOL_ARCHIVE)"; fi; \
	echo "upstream libsepol archive ready: $(ORLIXOS_LIBSEPOL_ARCHIVE)"

$(ORLIXOS_LIBSEPOL_ARCHIVE_STAMP): $(ORLIXOS_LIBSEPOL_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify libsepol source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(LIBSEPOL_SHA256)" "$(ORLIXOS_LIBSEPOL_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_LIBSEPOL_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_LIBSEPOL_ARCHIVE_STAMP)"; \
	echo "upstream libsepol ready: $(ORLIXOS_LIBSEPOL_ARCHIVE)"

$(ORLIXOS_LIBSEPOL_SOURCE_STAMP): $(ORLIXOS_LIBSEPOL_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_LIBSEPOL_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_LIBSEPOL_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_LIBSEPOL_SOURCE_STAMP)"; \
	echo "extracted libsepol source: $(ORLIXOS_LIBSEPOL_SRC_DIR)"

$(ORLIXOS_LIBSELINUX_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch libselinux source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify libselinux source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_LIBSELINUX_ARCHIVE)" ]; then curl -fL "$(LIBSELINUX_URL)" -o "$(ORLIXOS_LIBSELINUX_ARCHIVE)"; fi; \
	echo "upstream libselinux archive ready: $(ORLIXOS_LIBSELINUX_ARCHIVE)"

$(ORLIXOS_LIBSELINUX_ARCHIVE_STAMP): $(ORLIXOS_LIBSELINUX_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify libselinux source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(LIBSELINUX_SHA256)" "$(ORLIXOS_LIBSELINUX_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_LIBSELINUX_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_LIBSELINUX_ARCHIVE_STAMP)"; \
	echo "upstream libselinux ready: $(ORLIXOS_LIBSELINUX_ARCHIVE)"

$(ORLIXOS_LIBSELINUX_SOURCE_STAMP): $(ORLIXOS_LIBSELINUX_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_LIBSELINUX_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_LIBSELINUX_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	patch -d "$(ORLIXOS_LIBSELINUX_SRC_DIR)" -p1 < "$(PROJECT_DIR)/Sources/patches/libselinux-3.10-pthread-once-structured-type.patch"; \
	touch "$(ORLIXOS_LIBSELINUX_SOURCE_STAMP)"; \
	echo "extracted libselinux source: $(ORLIXOS_LIBSELINUX_SRC_DIR)"

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
