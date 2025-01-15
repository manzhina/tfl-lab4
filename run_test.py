import subprocess

def load_tests(filename):
    """Читает test.txt и возвращает два списка: positive_tests, negative_tests."""
    with open(filename, 'r', encoding='utf-8') as f:
        lines = f.read().splitlines()
    positive_tests = []
    negative_tests = []
    current_mode = None  # 'plus' или 'minus'
    for line in lines:
        line = line.strip()
        if not line:
            continue

        if line.startswith('+:'):
            current_mode = 'plus'
            continue
        elif line.startswith('-:'):
            current_mode = 'minus'
            continue
        
        if current_mode == 'plus':
            positive_tests.append(line)
        elif current_mode == 'minus':
            negative_tests.append(line)

    return positive_tests, negative_tests

def run_test(expression, should_be_correct):
    """
    Запускает программу regex_cpp_parser, подавая на стандартный ввод строку expression.
    Возвращает True, если результат (корректно / ошибка) совпал с ожиданиями, иначе False.
    """
    proc = subprocess.run(["./regex_cpp_parser"],
                          input=expression.encode('utf-8'),
                          capture_output=True)

    stdout_text = proc.stdout.decode('utf-8', errors='replace')
    stderr_text = proc.stderr.decode('utf-8', errors='replace')

    if "Ошибка:" in stdout_text or "Ошибка:" in stderr_text:
        is_correct = False
    else:
        is_correct = True

    return (is_correct == should_be_correct)

def main():
    positive_tests, negative_tests = load_tests("test.txt")

    total_tests = len(positive_tests) + len(negative_tests)
    passed_tests = 0

    print("==== Проверяем выражения, которые должны быть корректными ====")
    for expr in positive_tests:
        result = run_test(expr, True)
        print(f"Test: {expr} -> {'OK' if result else 'FAIL'}")
        if result:
            passed_tests += 1

    print("\n==== Проверяем выражения, которые должны выдавать ошибку ====")
    for expr in negative_tests:
        result = run_test(expr, False)
        print(f"Test: {expr} -> {'OK' if result else 'FAIL'}")
        if result:
            passed_tests += 1

    print(f"\nИтого пройдено {passed_tests} из {total_tests} тестов.")

if __name__ == "__main__":
    main()
