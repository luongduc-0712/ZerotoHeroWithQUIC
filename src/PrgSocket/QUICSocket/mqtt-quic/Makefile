.PHONY: all test integration broker preflight clean
all:
	$(MAKE) -C clients
test:
	$(MAKE) -C clients test
	python3 -m unittest discover -s tests -p 'test_*.py' -v
integration: all
	python3 tests/integration.py
broker:
	./scripts/broker.sh up
preflight:
	python3 scripts/preflight.py
clean:
	$(MAKE) -C clients clean
