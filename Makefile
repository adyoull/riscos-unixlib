# riscos-unixlib: shortcuts for the scripts in build/, tests/ and tools/.
# See docs/MAINTAINING.md.

GCCSDK_ENV ?= $(HOME)/gccsdk/env
export GCCSDK_ENV

.PHONY: help check check-lib sources lib install patches riscos-tests elf2aif clean

help:
	@echo "make check         host tests + patch checks (no cross compiler needed)"
	@echo "make sources       fetch and check GCCSDK and the GCC source (build/src)"
	@echo "make lib           build libunixlib.a (needs GCCSDK_ENV)"
	@echo "make install       build it and copy lib + headers into GCCSDK_ENV"
	@echo "make check-lib     check a built library/program for mismatched objects"
	@echo "make patches       regenerate patches/*.diff from the history"
	@echo "make riscos-tests  build tests/riscos/out/UnixLibTests.zip"
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

patches:
	tools/make-patches.sh

elf2aif:
	$(MAKE) -C tools/elf2aif

riscos-tests: elf2aif
	tests/riscos/build.sh

clean:
	rm -rf build/work tests/host/*/out tests/riscos/out
	$(MAKE) -C tools/elf2aif clean
