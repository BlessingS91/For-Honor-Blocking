from pathlib import Path
import shutil

BASE_DIR = Path(
    r"E:\ModLists\Skyrim Modding Setup\mods\ForHonorBlocking"
    r"\meshes\actors\character\animations\OpenAnimationReplacer"
)

OLD = b"CameraMovementFHB"
NEW = b"BlockDirectionFHB"

updated_files = 0
total_replacements = 0

for config in BASE_DIR.rglob("config.json"):
    data = config.read_bytes()

    count = data.count(OLD)
    if count == 0:
        continue

    # Create backup without overwriting an existing backup
    backup = config.with_name("config.json.bak")
    if not backup.exists():
        shutil.copy2(config, backup)

    # Replace only the graph variable name
    config.write_bytes(data.replace(OLD, NEW))

    updated_files += 1
    total_replacements += count

    print(f"Updated: {config.parent.name} ({count} replacements)")

print("\nFinished!")
print(f"Files updated: {updated_files}")
print(f"Total replacements: {total_replacements}")
