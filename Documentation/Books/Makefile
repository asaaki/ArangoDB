all: build-books

.PHONY: md-files

MDPP_FILES = $(wildcard *.mdpp */*.mdpp */*/*.mdpp)
MD_FILES = $(wildcard Users/*/*.md)

md-files:
	@for file in $(basename $(MDPP_FILES)); do echo "converting $${file}.mdpp"; markdown-pp.py $${file}.mdpp $${file}.md; done

clean-md-files:
	@for file in $(basename $(MD_FILES)); do echo "remove $${file}.md";rm $${file}.md;done

.PHONY: build-books

build-books: build-books-keep-md
	make clean-md-files

build-books-keep-md: md-files
	@test -d books || mkdir books
	make build-book NAME=Users

build-book:
	@test -d books/$(NAME) || mkdir books/$(NAME)
	python codeBlockReader.py
	python bot.py
	cd $(NAME) && gitbook build -o ../books/$(NAME)
	cp Users/Arangodb_Logo.png books/Users/
