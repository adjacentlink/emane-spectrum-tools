EXTRA_DIST = \
 $(MARKDOWN) \
 emane-spectrum-tools-ota-record.header \
 emane-spectrum-tools-monitor.header \
 pygments.theme

MARKDOWN = \
 emane-spectrum-tools-ota-record.md \
 emane-spectrum-tools-monitor.md

PDFS = \
 $(MARKDOWN:.md=.pdf)

if HAVE_PANDOC

all: $(PDFS)

%.pdf: %.header %.md
	pandoc \
	 $^ \
	 --number-sections \
	 --highlight-style pygments.theme \
	 -f markdown+raw_tex+header_attributes+multiline_tables \
	 -V colorlinks \
	 -o $@

else

all:

endif

clean-local:
	-rm -rf $(PDFS)
