# riscos-unixlib: shortcuts for the scripts in build/, tests/ and tools/.
# See docs/MAINTAINING.md.

# The cross toolchain: GCCSDK_ENV if set; in the riscos-crossdev container
# image, its toolchain ($RISCOS_TOOLCHAIN, the same GCCSDK GCC 10.2);
# otherwise ~/gccsdk/env.
GCCSDK_ENV ?= $(or $(RISCOS_TOOLCHAIN),$(HOME)/gccsdk/env)
export GCCSDK_ENV

.PHONY: help check check-lib sources lib install shared install-shared shared-tests check-shared patches riscos-tests elf2aif release shared-release clean

help:
	@echo "make check         host tests + patch checks (no cross compiler needed)"
	@echo "make sources       fetch and check GCCSDK and the GCC source (build/src)"
	@echo "make lib           build libunixlib.a (needs GCCSDK_ENV)"
	@echo "make install       build it and copy lib + headers into GCCSDK_ENV"
	@echo "make shared        build libunixlib.so.5 too (GCCSDK_ENV with shared libraries)"
	@echo "make install-shared  build both and install them, so programs can link dynamically"
	@echo "make shared-tests  build tests/riscos/out-shared (tests linked dynamically)"
	@echo "make check-shared  check the built .so against GCCSDK's ABI"
	@echo "make check-lib     check a built library/program for mismatched objects"
	@echo "make patches       regenerate patches/*.diff from the history"
	@echo "make riscos-tests  build tests/riscos/out/UnixLibTests.zip"
	@echo "make release TAG=vX.Y.Z  clean build + checks + release files in out/release"
	@echo "make shared-release TAG=vX.Y.Z  the shared library package in out/release"
	@echo "make clean         remove build output (keeps build/src)"

check:
	tests/check.sh

check-lib:
	tools/check-lib.sh $(FILES)

sources:
	build/fetch-sources.sh

lib:
	build/build-unixlib.sh

install:
	INSTALL=yes build/build-unixlib.sh

shared:
	SHARED=yes build/build-unixlib.sh

install-shared:
	SHARED=yes INSTALL=yes build/build-unixlib.sh

shared-tests:
	tests/riscos/build-shared.sh

check-shared:
	tools/check-shared-abi.sh build/work-shared/build/.libs/libunixlib.so.5

patches:
	tools/make-patches.sh

elf2aif:
	$(MAKE) -C tools/elf2aif

riscos-tests: elf2aif
	tests/riscos/build.sh

release: elf2aif
	tools/make-release.sh $(TAG)

shared-release:
	tools/make-shared-package.sh $(TAG)

clean:
	rm -rf build/work build/work-shared build/work-trace build/work-shared-trace \
	  tests/host/*/out tests/riscos/out tests/riscos/out-shared
	$(MAKE) -C tools/elf2aif clean
