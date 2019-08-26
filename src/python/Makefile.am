EXTRA_DIST= \
 AUTHORS \
 COPYING \
 MANIFEST.in \
 scripts \
 setup.py.in \
 scripts \
 emane_spectrum_tools

BUILT_SOURCES = \
 emane_spectrum_tools/interface/spectrummonitor_pb2.py \
 setup.py

all-local:
	$(PYTHON) setup.py build

clean-local: setup.py
	$(PYTHON) setup.py clean
	-rm -rf build dist
	-rm -f MANIFEST $(BUILT_SOURCES)
	-rm -f .installedfiles
	-find -name "*.pyc" -delete

dist-hook:
	find $(distdir) -name "*_pb2.py" -delete

sdist:
	$(PYTHON) setup.py sdist

edit = sed                              \
        -e 's|@VERSION[@]|$(VERSION)|g' 

emane_spectrum_tools/interface/spectrummonitor_pb2.py: ../libemane-spectrum-monitor/spectrummonitor.proto
	protoc -I../libemane-spectrum-monitor --python_out=emane_spectrum_tools/interface  $<

setup.py:	setup.py.in
	if test -f $@; then chmod u+w $@; fi
	$(edit) $< > $@
	chmod g-w,u-w $@

install-exec-hook: $(BUILT_SOURCES)
	$(PYTHON) setup.py install \
	    -O1 \
        --record .installedfiles \
	    --prefix=$(prefix) \
	    --exec-prefix=$(exec_prefix) \
	    $(if $(DESTDIR),--root=$(DESTDIR))

uninstall-hook:
	if test -f .installedfiles; then xargs -a .installedfiles rm -f; fi
