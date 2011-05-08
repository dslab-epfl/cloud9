RST_FILES := $(shell find . -name "*.rst")
HTML_FILES := $(RST_FILES:.rst=.html)

.PHONY: all clean

all: $(HTML_FILES)

clean:
	rm -f $(HTML_FILES)

%.html: %.rst
	rst2html $< $@