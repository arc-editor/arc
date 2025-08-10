import re
import sys

def update_readme(readme_file, table_file):
    with open(readme_file, 'r') as f:
        readme_content = f.read()

    with open(table_file, 'r') as f:
        benchmark_table = f.read()

    # Ensure the placeholder exists
    if '<!-- BENCHMARK_TABLE_START -->' not in readme_content:
        print(f"Error: Placeholder '<!-- BENCHMARK_TABLE_START -->' not found in {readme_file}")
        sys.exit(1)

    new_readme_content = re.sub(
        r'<!-- BENCHMARK_TABLE_START -->(.|\n)*<!-- BENCHMARK_TABLE_END -->',
        f'<!-- BENCHMARK_TABLE_START -->\n{benchmark_table}\n<!-- BENCHMARK_TABLE_END -->',
        readme_content
    )

    with open(readme_file, 'w') as f:
        f.write(new_readme_content)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python update_readme.py <path_to_readme> <path_to_table>")
        sys.exit(1)

    update_readme(sys.argv[1], sys.argv[2])
    print(f"Successfully updated {sys.argv[1]} with content from {sys.argv[2]}")
