isolation:
	@echo "Adding isolation support ..."
	@put-file "$(ROOTDIR)" $(ISOLATION_FILES)
	@put-tree "$(ROOTDIR)" $(ISOLATION_DATADIR)

pack: isolation
