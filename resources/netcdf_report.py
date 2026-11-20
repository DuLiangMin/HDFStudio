"""Read-only report worker. Usage: python netcdf_report.py summary|time|dates|cf FILE [VARIABLE]."""
import sys

import netCDF4 as nc
import numpy as np


def orientation(var):
    units = str(getattr(var, 'units', '')).lower().replace('_', '')
    standard = getattr(var, 'standard_name', '')
    if units in ('degreesnorth', 'degreenorth', 'degreesn', 'degreen') and standard in ('', 'latitude'):
        directions = ('south to north', 'north to south')
    elif units in ('degreeseast', 'degreeeast', 'degreese', 'degreee') and standard in ('', 'longitude'):
        directions = ('west to east', 'east to west')
    elif standard in ('latitude', 'longitude'):
        return 'direction unknown (missing or inconsistent angular units)'
    else:
        return None
    if var.ndim != 1 or var.size < 2:
        return 'direction undefined for this shape'
    increasing = decreasing = True
    previous = first = None
    for start in range(0, var.size, 65536):
        chunk = np.ma.asarray(var[start:start + 65536], dtype=np.float64)
        if np.ma.is_masked(chunk) or not np.isfinite(chunk.data).all():
            return 'direction unknown (missing or invalid coordinate)'
        values = chunk.data
        if first is None:
            first = values[0]
        if previous is not None:
            values = np.r_[previous, values]
        previous = values[-1]
        delta = np.diff(values)
        increasing &= bool(np.all(delta > 0))
        decreasing &= bool(np.all(delta < 0))
    order = directions[0] if increasing else directions[1] if decreasing else 'no single numeric direction (wrap, repeat or reversal)'
    return f'{order}, first {first:g}, last {previous:g} {var.units}'


def attributes(obj):
    for key in obj.ncattrs():
        print(f'    {key} = {obj.getncattr(key)}')


def summary(group):
    print(f'Group {group.path}  data_model = {group.data_model}')
    attributes(group)
    print('dimensions(sizes):', ', '.join(f'{k}({len(v)})' for k, v in group.dimensions.items()))
    print('variables(dimensions):')
    for name, var in group.variables.items():
        print(f'  {name} {var.dtype} {var.dimensions} shape={var.shape}')
        attributes(var)
        order = orientation(var)
        if order:
            print(f'    Stored order {group.path.rstrip("/")}/{name}: {order}')
    for child in group.groups.values():
        summary(child)


def time_list(var):
    if var.ndim != 1:
        raise ValueError('Time coordinate must be one-dimensional')
    calendar = getattr(var, 'calendar', 'standard')
    print(f'{var.name} | {var.units} | calendar={calendar}')
    print('Index\tValue\tDate\tDate and time')
    limit = min(var.size, 10000)
    values = var[:limit]
    for index, value in enumerate(values):
        if np.ma.is_masked(value) or not np.isfinite(value):
            print(f'{index}\tmissing')
            continue
        moment = nc.num2date(value, var.units, calendar=calendar)
        print(f'{index}\t{value}\t{moment:%Y%m%d}\t{moment:%Y%m%d %H:%M:%S}')
    if limit < var.size:
        print(f'Showing first {limit} of {var.size} values.')


def date_columns(var):
    """Machine-readable output for the grid table: a header line, then one tab-separated
    'yyyymmdd<TAB>yyyymmdd hh:mm:ss' line per value. Exit code 3 means the variable is not a
    'UNIT since DATE' time coordinate, so the table keeps its numeric column only."""
    units = getattr(var, 'units', None)
    if var.ndim != 1 or not isinstance(units, str) or ' since ' not in units.lower():
        sys.exit(3)
    calendar = getattr(var, 'calendar', 'standard')
    print(f'#\t{units}\t{calendar}')
    for start in range(0, var.size, 65536):
        chunk = np.ma.masked_invalid(np.ma.asarray(var[start:start + 65536], dtype=np.float64))
        for value in chunk:
            if value is np.ma.masked:
                print('missing\tmissing')
                continue
            moment = nc.num2date(float(value), units, calendar=calendar)
            print(f'{moment:%Y%m%d}\t{moment:%Y%m%d %H:%M:%S}')


def cf_report(path):
    from compliance_checker.runner import ComplianceChecker
    from compliance_checker.suite import CheckSuite
    CheckSuite.load_all_available_checkers()
    passed, errors = ComplianceChecker.run_checker(path, ['cf:1.8'], 1, 'normal')
    result = 'INCOMPLETE' if errors else 'PASS' if passed else 'FAIL'
    print(f'\nCF-1.8 result: {result} (IOOS Compliance Checker, normal criteria)')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    mode, path = sys.argv[1:3]
    if mode == 'cf':
        cf_report(path)
    else:
        with nc.Dataset(path, 'r') as dataset:
            if mode == 'summary':
                summary(dataset)
            elif mode == 'dates':
                date_columns(dataset[sys.argv[3]])
            else:
                time_list(dataset[sys.argv[3]])
