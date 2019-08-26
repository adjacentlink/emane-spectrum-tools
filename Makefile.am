ACLOCAL_AMFLAGS = -I m4

SUBDIRS= \
 src     

EXTRA_DIST =    \
 AUTHORS        \
 COPYING        \
 ChangeLog      \
 NEWS           \
 README         \
 README.md      \
 SPONSOR        \
 autogen.sh     \
 $(PACKAGE).spec.in \
 debian         \
 images

dist-hook:
	find $(distdir) -name "*.pb.*" -delete

clean-local:
	-rm -f $(PACKAGE)-*.tar.gz
	-rm -f $(PACKAGE).spec
	-rm -rf .rpmbuild
	-rm -rf .debbuild

edit = sed \
        -e 's|@VERSION[@]|$(VERSION)|g' \
        -e 's|@DEBIAN_VERSION[@]|$(DEBIAN_VERSION)|g' \
        -e 's|@DATE_RFC2822[@]|$(DATE_RFC2822)|g'

RPMARCH=`arch`

if HAVE_RPMBUILD

rpm: $(PACKAGE).spec dist-gzip
	mkdir -p .rpmbuild/BUILD \
	.rpmbuild/SPECS \
	.rpmbuild/SOURCES \
	.rpmbuild/SRPMS \
	.rpmbuild/RPMS/noarch \
	.rpmbuild/tmp
	cp -f $(PACKAGE)-$(VERSION).tar.gz .rpmbuild/SOURCES
	rpmbuild --clean -ba $(top_srcdir)/$(PACKAGE).spec --target $(RPMARCH) \
    --define "_topdir $$PWD/.rpmbuild" \
    --define "_tmppath $$PWD/.rpmbuild/tmp"
	@echo "============================================================"
	@echo "RPMs located in .rpmbuild/RPMS and .rpmbuild/SRPMS"

$(PACKAGE).spec: $(PACKAGE).spec.in
	if test -f $@; then chmod u+w $@; fi
	$(edit) $< > $@
	chmod g-w,u-w $@

endif

if HAVE_DEB

deb: dist-gzip
	mkdir -p .debbuild
	cp -f $(PACKAGE)-$(VERSION).tar.gz .debbuild
	cd .debbuild && tar xzf $(PACKAGE)-$(VERSION).tar.gz
	$(edit) .debbuild/$(PACKAGE)-$(VERSION)/debian/changelog.in > \
          .debbuild/$(PACKAGE)-$(VERSION)/debian/changelog
	cd .debbuild/$(PACKAGE)-$(VERSION) && dpkg-buildpackage -F -us -uc
	@echo "============================================================"
	@echo "debs located in .debbuild"
endif
