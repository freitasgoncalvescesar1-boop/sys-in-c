# Exemplo pythont 6.0 - List Comprehensions, with open() & try/except

print("=== Testando pythont 6.0 (List Comprehensions & with) ===")

# 1. List Comprehensions com range() e com lista existente
numeros = [1, 2, 3, 4, 5]
dobros = [x * 2 for x in numeros]
quadrados = [i * i for i in range(1, 6)]
pares = [x for x in numeros if x % 2 == 0]

print(f"Lista Original: {numeros}")
print(f"Dobros [x * 2]: {dobros}")
print(f"Quadrados [i*i]: {quadrados}")
print(f"Apenas Pares [if x % 2 == 0]: {pares}")

# 2. Gerenciador de Contexto (with open)
with open("saida_with.txt", "w") as f:
    f.write("Gravado com sucesso pelo 'with open()' do pythont 6.0!\n")
    f.write("Arquivo fechado automaticamente sem memory leak.\n")

with open("saida_with.txt", "r") as f_leitura:
    conteudo = f_leitura.read()

print("Conteudo Lido com with open():")
print(conteudo)

# 3. Tratamento de Excecoes (try / except)
try:
    x = 100
    print(f"Bloco try executado com seguranca! Valor: {x}")
except:
    print("Erro capturado!")

print("Finalizado com 100% de sucesso no pythont 6.0!")
