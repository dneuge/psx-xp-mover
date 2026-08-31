import sys
import os.path
tool_dir = os.path.dirname(__file__)+os.sep+os.pardir
sys.path.append(tool_dir + os.sep + 'simple-sbom' + os.sep + 'python')

from simple_sbom import *

UTF_8_FILE_ENCODING = 'utf-8'

root_dir = tool_dir + os.sep + os.pardir
release_dir = root_dir + os.sep + 'release'

dist_dir = root_dir + os.sep + 'dist'
license_trademarks_path = dist_dir + os.sep + 'LICENSES.trademarks.txt'

licenses_dir = root_dir + os.sep + 'licenses'
spdx_licenses_dir = licenses_dir + os.sep + 'spdx'

license_out_path = release_dir + os.sep + 'LICENSES.txt'

def load_text_file(path:str) -> str:
    if not os.path.exists(path):
        raise ValueError(f'File does not exist: {path}')

    with open(path, 'r', encoding=UTF_8_FILE_ENCODING) as fh:
        return fh.read()

def load_spdx_license(spdx_id:str) -> str:
    return load_text_file(spdx_licenses_dir + os.sep + spdx_id + '.txt')

def enumerate_sentence(conjunction:str, items:Iterable[str]) -> str:
    items = list(items)
    if len(items) == 0:
        raise ValueError('at least one item is required')
    elif len(items) == 1:
        return items[0]
    else:
        return ', '.join(items[:-1]) + ' ' + conjunction + ' ' + items[-1]

def format_years(years:Iterable[CopyrightYear]) -> str:
    out = ''

    for year in years:
        if out != '':
            out += ', '

        if not isinstance(year, tuple):
            out += str(year)
        else:
            out += f'{year[0]}-{year[1]}'

    return out

def format_legal_successors(author:LegalEntity) -> str:
    out : str = ''

    for info in author.successors:
        successor : LegalEntity = info.entity

        out += f'{author.name} has been succeeded by {successor.name}'
        if info.date is not None:
            out += f' in {info.date.year}'
        out += '\n'

        out += format_legal_successors(successor)

    return out.rstrip()

def format_copyright(item:Copyright) -> str:
    out : str = ''

    # Prefer the copyright information that is most accurate for reproduction:
    #   1. original remarks (unmodified)
    #   2. combined author name representations (usually unmodified)
    #   3. individual author names (as per LegalEntity)
    if len(item.original_remarks) > 0:
        for remark in item.original_remarks:
            # add an empty line to space multi-line text blocks apart from any previous collated one-liners or other blocks
            has_multiple_lines = '\n' in remark
            if has_multiple_lines and len(out) > 0:
                out += '\n'
            out += remark.rstrip() + '\n\n'
    else:
        author_names: str = ''
        if item.combined_authors is not None and len(item.combined_authors) > 0:
            author_names = item.combined_authors
        else:
            author_names = enumerate_sentence('and', [author.name for author in item.authors])

        out += 'Copyright (c) '
        if len(item.years) > 0:
            out += format_years(item.years) + ' '
        out += author_names

    # TODO: collect and list successors separately (avoid duplicates, show at end of overall copyright notice)
    for author in item.authors:
        successor_out: str = format_legal_successors(author)
        if successor_out != '':
            out += '\n\n'
            out += successor_out

    return out

def prefix_all_lines(prefix:str, s:str) -> str:
    out : str = ''

    for line in s.splitlines():
        out += prefix + line + '\n'

    return out

def format_captioned_divider(s):
    out = DIVIDER[:3]+' ' + s + ' '
    out += DIVIDER[len(out):]
    return out

license_full_names : dict[str,str] = {}
license_short_names : dict[str,str] = {}
license_texts : dict[str,str|None] = {}

# Windows SDK EULA is only available by downloading its installer and shouldn't be relevant to end-users running
# genuine, properly licensed copies of Windows which has already been stated in the binary distribution license of
# this plugin. Allow the text to be omitted.
license_texts['EULAID:WIN10SDK.RTM.AUG_2018_en-US'] = None

# import all licenses declared in SBOM
sbom_path = root_dir + os.sep + 'sbom.xml'
print(f'Loading {sbom_path}')
sbom = SimpleSBOM.parse_file(sbom_path)
for license in sbom.licenses.values():
    text : str|None = license.text
    if text is None and license.id not in license_texts:
        if license.standard is None:
            raise ValueError(f'SBOM-defined license "{license.id}" has no text but is also non-standard')

        if license.standard.spdx is None:
            raise ValueError(f'SBOM-defined license "{license.id}" has no text but also has no SPDX identifier')

        if len(license.standard.variations) > 0:
            raise ValueError(f'SBOM-defined license "{license.id}" has variations deviating from standard SPDX text, unable to apply generic license')

        text = load_spdx_license(license.standard.spdx)

    license_full_names[license.id] = license.name or license.short_name or license.id
    license_short_names[license.id] = license.short_name or license.id
    license_texts[license.id] = text

print('Formatting licenses...')

# establish alphabetic license order by name
license_ids = list(license_texts.keys())
license_ids.sort(key=lambda license_id: (license_full_names.get(license_id) or license_id).lower())

DIVIDER=('-'*80)
license_out : list[str] = [
    'This file gathers all applicable license information for PSX/XP Mover.',
    'By installing and using this plugin you accept all terms laid out in this',
    'document in addition to the separate DISCLAIMER.txt file.',
]
license_out.append('')
license_out.append(format_captioned_divider('PSX/XP Mover Binary Distribution License'))
license_out.append('')
license_out += load_text_file(licenses_dir + os.sep + 'xpmover-binary-distribution.txt').split(r'\R')
license_out.append('')
license_out.append(format_captioned_divider('PSX/XP Mover Source Code License'))
license_out.append('')
license_out += load_text_file(root_dir + os.sep + 'LICENSE.md').split(r'\R')
license_out.append('')
license_out.append(format_captioned_divider('PSX/XP Mover Software Dependencies'))
license_out.append('')
license_out.append('PSX/XP Mover uses the following dependencies, in alphabetic order,')
license_out.append('whose licenses are reproduced in the following sections:')
license_out.append('')

tag_descriptions : dict[str, str] = {
    # all tags used in SBOM must be listed here
    # will be concatenated to "only used if ... [or ...]"
    'target-macos': 'compiled for macOS',
    'target-windows': 'compiled for Windows®',
}

def split_lines(s:str, prefix:str=''):
    if prefix != '':
        s = prefix_all_lines(prefix, s)
    return s.split(r'\R')

for dependency in sorted(sbom.dependencies.values(), key=lambda x: x.name.lower()):
    if dependency.method == DependencyMethod.PROVIDED:
        print(f'Dependency is provided, skipping: {dependency.id}')
        continue

    out_activation : str = ''
    has_activation : bool = len(dependency.activation_tags) > 0
    if has_activation:
        for tag in dependency.activation_tags:
            if out_activation != '':
                out_activation += ' or '
            out_activation += tag_descriptions[tag].strip()
        out_activation = 'only used if ' + out_activation

    out_url : str|None = None
    if len(dependency.websites) > 0:
        out_url = dependency.websites[0]

    #license_out.append(' - '+dependency.name+(' ['+out_activation+']' if has_activation else ''))
    license_out.append(' - '+dependency.name)
    if out_activation != '':
        license_out.append('   ['+out_activation+']')
    if dependency.version is not None:
        license_out.append('   version: ' + dependency.version)
    if out_url is not None:
        license_out.append('   website: ' + out_url)

    # collect all copyrights before formatting output
    # tuple is license ID + copyright text (multi-line)
    copyrights : list[tuple[str|None,str]] = []
    for item in dependency.copyrights:
        license_id : str|None = item.license.id if item.license is not None else None
        text : str = format_copyright(item)
        copyrights.append((license_id, text))

    if len(dependency.patches) > 0:
        text : str = 'With additional patches:\n'

        for patch in dependency.patches:
            text += ' - ' + patch.description + '\n'
            for item in patch.copyrights:
                if item.license is not None:
                    text += '   made available under '+item.license.name+' license\n'
                text += prefix_all_lines('   ', format_copyright(item))

        copyrights.append((None, text.rstrip()))

    previous_license_id = None
    for license_id, text in sorted(copyrights):
        if license_id != previous_license_id:
            license_out.append('   released under '+license_short_names[license_id]+' license:')

        license_out += split_lines(text.rstrip(), prefix='     ')

        previous_license_id = license_id

    if dependency.excerpt is not None:
        license_out.append('   ---')
        license_out += split_lines(dependency.excerpt.rstrip(), prefix='   ')

for license_id in license_ids:
    text = license_texts[license_id]
    if text is None:
        continue

    license_out.append('')
    license_out.append('')
    license_out.append(format_captioned_divider(license_full_names[license_id]))
    license_out.append('')
    license_out += text.rstrip().split(r'\R')

# add static trademarks as used within the license file only
license_out.append('')
license_out.append('')
license_out.append(format_captioned_divider('Trademarks'))
license_out.append('')
license_out += load_text_file(license_trademarks_path).split(r'\R')

print(f'Writing {license_out_path}')
with open(license_out_path, 'w', encoding=UTF_8_FILE_ENCODING) as fh:
    fh.write('\n'.join(license_out))
print('... done')
