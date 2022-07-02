MODULE=mod_sqlite
MODULE_NAME=sqlite
APXS=/usr/bin/apxs

all: $(MODULE)

mod_sqlite:
	$(APXS) -Wc,-g -c $@.c -lsqlite3

install: $(MODULE)
	$(APXS) -i -a -n $(MODULE_NAME) $(MODULE).la

clean:
	rm -rf *.so *.o *.slo *.la *.lo .libs
