import subprocess
from genericpath import isfile
import os
import sys

if len(sys.argv) < 2:
    print("Error, starting directory must be specified")
    exit(1)


# "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Stormworks\\rom\\meshes\\"
BASE_DIR = sys.argv[1]


def cvtdir(d):
    files = [f for f in os.listdir(d) if f.endswith('.mesh')]
    directories = [f for f in os.listdir(d) if os.path.isdir(d + f)]

    for file in files:
        if os.path.isfile(d + file.split('.')[0] + '.ply'):
            continue  # Already converted
        print(subprocess.run([
            f"./build/swmeshexp.exe", f"{d + file}"], stdout=subprocess.PIPE).stdout.decode('utf-8'))
        # # print(f"Converted {d + file.split('.')[0] + '.ply'}")
        # print(d + file)
        pass

    for directory in directories:
        cvtdir(d+directory + '\\')


cvtdir(BASE_DIR)
