# Exemplo Avançado de Python para o pythont 2.0

print("=== Testando pythont 2.0 (Listas & Built-ins) ===")

numeros = [10, 20, 30, 40, 50]
print("Primeiro elemento:", numeros[0])
print("Terceiro elemento:", numeros[2])

numeros[2] = 99
print("Terceiro elemento modificado:", numeros[2])

soma = 0
for i in range(1, 6):
    soma += i
print("Soma com += (1 a 5):", soma)

fatorial = 1
for x in range(1, 6):
    fatorial *= x
print("Fatorial de 5 com *= :", fatorial)

val_a = 42
val_b = 100
val_neg = -500

print("Maximo entre 42 e 100:", max(val_a, val_b))
print("Minimo entre 42 e 100:", min(val_a, val_b))
print("Valor absoluto de -500:", abs(val_neg))

print("Contando com break:")
for n in range(1, 10):
    if n == 4:
        break
    print("Contagem:", n)
