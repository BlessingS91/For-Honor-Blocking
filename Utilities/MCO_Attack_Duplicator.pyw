import os
import shutil
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox

SOURCE_FILES = {
    "mco_attack1.hkx": "MCO_Attack",
    "mco_powerattack1.hkx": "MCO_PowerAttack",
}


def duplicate_attacks():
    folder = filedialog.askdirectory(title="Select your MCO moveset folder")
    if not folder:
        return

    created = 0
    skipped = 0
    sources = 0
    errors = []

    for current_folder, _, filenames in os.walk(folder):
        existing = {name.lower() for name in filenames}

        for filename in filenames:
            prefix = SOURCE_FILES.get(filename.lower())
            if prefix is None:
                continue

            sources += 1
            source = Path(current_folder) / filename

            for number in range(2, 11):
                destination_name = f"{prefix}{number}.hkx"
                if destination_name.lower() in existing:
                    skipped += 1
                    continue

                destination = Path(current_folder) / destination_name
                try:
                    shutil.copy2(source, destination)
                    existing.add(destination_name.lower())
                    created += 1
                except OSError as exc:
                    errors.append(f"{destination}: {exc}")

    summary = (
        f"Source files found: {sources}\n"
        f"Copies created: {created}\n"
        f"Existing files skipped: {skipped}\n"
        f"Errors: {len(errors)}"
    )
    if errors:
        summary += "\n\nFirst errors:\n" + "\n".join(errors[:5])
        messagebox.showwarning("Finished with errors", summary)
    else:
        messagebox.showinfo("Finished", summary)


root = tk.Tk()
root.title("MCO Attack Duplicator")
root.geometry("370x145")
root.resizable(False, False)

tk.Label(
    root,
    text="Copies MCO_Attack1 and MCO_PowerAttack1\n"
         "to versions 2–10 in every matching subfolder.",
    justify="center",
).pack(pady=(18, 12))

tk.Button(
    root,
    text="Select Folder and Duplicate",
    command=duplicate_attacks,
    width=30,
    height=2,
).pack()

root.mainloop()
