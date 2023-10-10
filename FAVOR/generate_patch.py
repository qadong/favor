import json
import os
import subprocess
import glob

def apply_patches_to_source(json_file_path, source_dir):
    # Load the patches.json file
    with open(json_file_path, 'r') as f:
        patches_data = json.load(f)

    # Create a directory to store the generated patch files
    os.makedirs(os.path.join(source_dir, 'patches'), exist_ok=True)

    # Iterate through all source files that match the pattern func*.c or func*.cpp
    for filepath in glob.glob(os.path.join(source_dir, 'func*.c')) + glob.glob(os.path.join(source_dir, 'func*.cpp')):
        # Extract the file extension
        _, file_extension = os.path.splitext(filepath)

        if file_extension not in ['.c', '.cpp']:
            continue

        # Read the source file
        with open(filepath, 'r') as f:
            lines = f.readlines()

        # Process each patch name and its corresponding content
        for patch_name, patch_content in patches_data.items():
            # Split the patch content using the <S2SV_ModStart> marker
            patch_parts = patch_content.split('<S2SV_ModStart>')
            patch_parts = [part for part in patch_content.split('<S2SV_ModStart>') if part]
            # Keep track of the number of bug locations found to split the patch content
            found_bug_locs = 0

            # Apply the patches
            for idx, line in enumerate(lines):
                if '/*Bug_Line*/' in line and found_bug_locs < len(patch_parts):
                    lines[idx] = lines[idx].replace('/*Bug_Line*/', patch_parts[found_bug_locs].strip())
                    found_bug_locs += 1

            # Generate the new source file
            patch_name = patch_name.replace(' ', '')
            new_filepath = os.path.join(source_dir, f'{patch_name}{file_extension}')
            with open(new_filepath, 'w') as f:
                f.writelines(lines)

            # Generate the unified diff format patch file
            patch_filepath = os.path.join(source_dir, 'patches', f'{patch_name}.patch')

            # Corrected subprocess command
            with open(patch_filepath, 'w') as f:
                subprocess.run(['diff', '-u', filepath, new_filepath], stdout=f)

if __name__ == "__main__":
    json_file_path = 'patches.json'
    source_dir = './'
    apply_patches_to_source(json_file_path, source_dir)

