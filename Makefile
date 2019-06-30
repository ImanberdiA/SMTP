.SUFFIXES:

DEFINE_FLAGS=
DEFINE_FLAGS+=-D_POSIX_C_SOURCE=200809L
DEFINE_FLAGS+=-D_GNU_SOURCE

INCLUDE_FLAGS=
INCLUDE_FLAGS+=-Iinclude

CC=gcc
CFLAGS+=-std=c99
CFLAGS+=-Wall -Werror -pedantic
CFLAGS+=$(DEFINE_FLAGS)
CFLAGS+=$(INCLUDE_FLAGS)
CFLAGS+=-lcares
CFLAGS+=-lpthread
CFLAGS+=-g -O0

$(shell mkdir -p .tmp/client)

client: $(patsubst src/%.c,.tmp/client/%.o,$(wildcard src/*.c))
	$(CC) $(CFLAGS)	$^ -o $@

.tmp/client/%.o: src/%.c .tmp/client/%.d
	$(CC) -c $(CFLAGS) -MT $@ -MMD -MP -MF .tmp/client/$*.d.tmp -o $@ $< 
	mv -f .tmp/client/$*.d.tmp .tmp/client/$*.d
	@touch $@

# fake for dependency graph generation
.tmp/client/%.d: src/%.c
	@$(CC) -M $< $(CFLAGS) -o $@

include $(patsubst src/%.c,.tmp/client/%.d,$(wildcard src/*.c))

.PHONY:
# test_system: client tests/system.py
# 	pipenv run tests/system.py

test_system: client

$(shell mkdir -p .tmp/report)

report.pdf: report.tex $(wildcard report/*.tex) $(wildcard report/*.pdf) \
		$(patsubst report/%.dot,.tmp/report/%.pdf,$(wildcard report/*.dot)) \
		.tmp/report/dependencies.pdf \
		.tmp/report/flow.pdf \
		.tmp/report/test_system.log \
		.tmp/report/doxy/latex/refman.pdf
	# twice for correct TOC generation
	pdflatex --output-directory=.tmp $<
	pdflatex --output-directory=.tmp $< 
	cp .tmp/report.pdf .

.tmp/report/%.pdf: report/%.dot
	dot -Tpdf $^ -o $@

.tmp/report/dependencies.pdf: .tmp/report/dependencies.dot
	dot -Tpdf $^ -o $@

.tmp/report/dependencies.dot: Makefile report/tools/makesimple \
									   report/tools/makefile2dot
	report/tools/makesimple < $^ | report/tools/makefile2dot client > $@

.tmp/report/flow.pdf: .tmp/report/flow.dot
	dot -Tpdf $^ -o $@

.tmp/report/flow.dot: $(wildcard src/*.c) \
		report/cflow.ignore report/tools/cflow2dot
	cflow $(DEFINE_FLAGS) $(INCLUDE_FLAGS) -I/usr/include --level "0= " \
			$(wildcard src/*.c) | \
		grep -vf report/cflow.ignore | report/tools/cflow2dot > $@

.tmp/report/test_system.log: client tests/system.py
	pipenv run tests/system.py >$@ 2>&1

.tmp/report/doxy/latex/refman.pdf: report/Doxyfile \
		$(wildcard include/*.h) $(wildcard src/*.c)
	doxygen $<
	cd .tmp/report/doxy/latex && make

.PHONY:
all: client test_system report.pdf

.PHONY:
clean:
	$(RM) -r .tmp
	$(RM) client
	$(RM) report.pdf

