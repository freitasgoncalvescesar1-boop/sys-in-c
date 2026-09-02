# Exemplo de Script Python para Transpilar para C Nativo

def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

print("=== Teste de Execucao pythont ===")

total = 0
for i in range(1, 11):
    total += i

print("Soma de 1 a 10:", total)
print("Fibonacci de 10:", fibonacci(10))

for step in range(0, 15, 3):
    print("Contador com passo 3:", step)
