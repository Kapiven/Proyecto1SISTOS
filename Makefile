all: servidor cliente

servidor: servidor.c protocolo.h
	gcc -o servidor servidor.c -lpthread

cliente: cliente.c protocolo.h
	gcc -o cliente cliente.c -lpthread

clean:
	rm -f servidor cliente