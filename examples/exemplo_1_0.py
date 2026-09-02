# =====================================================================
# Exemplo Master Oficial - pythont 1.0-release
# =====================================================================

print("=================================================================")
print("  🚀 TESTE MASTER DO PYTHONT 1.0-RELEASE (NATIVE JIT ENGINE)")
print("=================================================================")

# 1. Lambdas
quadrado = lambda x: x * x
soma = lambda a, b: a + b

print(f"• Lambda quadrado(9) = {quadrado(9)}")
print(f"• Lambda soma(15, 25) = {soma(15, 25)}")

# 2. List Comprehensions (com range, listas e filtros if)
numeros = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
dobros = [x * 2 for x in numeros]
pares = [x for x in numeros if x % 2 == 0]
quads = [i * i for i in range(1, 6)]

print(f"• Lista Base: {numeros}")
print(f"• Dobros [x*2]: {dobros}")
print(f"• Pares [if x % 2 == 0]: {pares}")
print(f"• Quadrados de 1 a 5: {quads}")

# 3. Metodos de String & Built-ins
texto = "   linguagem python transpilada para c nativo   "
limpo = texto.strip()
print(f"• String limpa (.strip()): '{limpo}'")
print(f"• Titulo (.title()): '{limpo.title()}'")
print(f"• Comeca com 'linguagem': {limpo.startswith('linguagem')}")
print(f"• Termina com 'nativo': {limpo.endswith('nativo')}")
print(f"• Contagem de 'a': {limpo.count('a')}")
print(f"• Binario de 42: {bin(42)} | Hex: {hex(42)} | Oct: {oct(42)}")
print(f"• ASCII char(65): '{chr(65)}' | ord('A'): {ord('A')}")

# 4. Dicionarios Avancados
config = {'modo': 'producao', 'nivel': 99, 'versao': '1.0-release'}
print(f"• Chaves do Dict (.keys()): {config.keys()}")
print(f"• Valores do Dict (.values()): {config.values()}")
print(f"• get com valor default: {config.get('servidor', 'AWS-SaEast')}")

# 5. Gerenciador de Contexto (with open) & Leitura
with open("release_notes.txt", "w") as f:
    f.write("PYTHONT 1.0-RELEASE COMPILADO COM SUCESSO!\n")
    f.write("Executando a velocidade nativa de maquina sem overhead.\n")

with open("release_notes.txt", "r") as f_in:
    conteudo = f_in.read()

print("• Conteudo gravado e lido via with open():")
print(conteudo)

# 6. Excecoes (try / except)
try:
    status_code = 200
    print(f"• Bloco try executado! Status: {status_code}")
except:
    print("• Erro capturado com sucesso!")

print("=================================================================")
print("  ✔ PYTHONT 1.0-RELEASE EXECUTADO COM 100% DE SUCESSO!")
print("=================================================================")
