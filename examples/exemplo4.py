# Exemplo pythont 3.5 - Dicionarios (dict) & Slicing de Strings

print("=== Testando pythont 3.5 (Dicionarios & Slicing) ===")

usuario = {
    "nome": "Cesar",
    "idade": 20,
    "cidade": "Sao Paulo",
    "saldo": 1500.50
}

print(f"Nome do usuario: {usuario['nome']}")
print(f"Idade original: {usuario['idade']}")

# Modificando e adicionando campos
usuario["idade"] = 21
usuario["profissao"] = "Dev C / Python"

print(f"Idade atualizada: {usuario['idade']}")
print(f"Nova profissao: {usuario['profissao']}")

# Testando Slicing de String
linguagem = "PythonTranspiler"
print(f"Prefixo (0:6): {linguagem[0:6]}")
print(f"Sufixo (6:): {linguagem[6:]}")
print(f"Reverso ([::-1]): {linguagem[::-1]}")

print("Finalizado com sucesso no pythont 3.5!")
