# Exemplo pythont 3.0 - F-Strings, Listas Dinamicas & Matematica

print("=== Testando pythont 3.0 (F-Strings & Listas) ===")

nome = "Cesar"
idade = 20
print(f"Ola {nome}! Sua idade e {idade} anos.")

valores = [10, 20, 30]
valores.append(40)
valores.append(50)

print(f"Tamanho da lista: {len(valores)}")
print(f"Soma dos elementos: {sum(valores)}")

# Calculo matematico
raiz = math.sqrt(144)
print(f"Raiz quadrada de 144 com math.sqrt: {raiz}")

print("Contagem regressiva com F-String:")
for x in range(5, 0, -1):
    print(f"Passo: {x}...")

print("Finalizado com sucesso!")
