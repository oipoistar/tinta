# Field notes

Notes from three weeks of testing inks on different papers. Close this tab,
press **Ctrl+Shift+T**, and it should come back where it stood, scrolled to
where you left it.

## Papers

| Paper | Weight | Feathering | Dry time |
| ----- | -----: | ---------- | -------: |
| Tomoe River | 52 g/m² | none | 40 s |
| Rhodia | 80 g/m² | slight | 18 s |
| Copy paper | 75 g/m² | heavy | 9 s |
| Midori MD | 81 g/m² | none | 25 s |

## Observations

1. Shading shows best on the coated papers.
2. Sheen needs a wet nib *and* a slow paper:
   - Tomoe River sheens with almost anything;
   - Rhodia only with the saturated reds;
   - copy paper never.
3. Dry time roughly follows absorbency, $t \approx k / a$, with $k$ an ink constant.

> A good ink forgives a bad pen; a good paper forgives a bad ink.
> Nothing forgives both.

```python
def dry_time(absorbency, k=720):
    """Seconds until the line stops smearing."""
    return k / max(absorbency, 1)
```

## Day by day

### Week one

The first week went to the blues. Each was written in the same pangram,
three lines per paper, and left for an hour before the smear test. The
results were dull and consistent, which is what one wants from a blue.

### Week two

The reds behaved worse. Two of them feathered on Rhodia, which almost
nothing does, and one stained the converter so badly that it now lives in
a jar with the cleaning solution.

### Week three

Iron gall inks, finally. They start pale and darken over a day; the scans
below were taken after twenty-four hours. Keep them away from steel nibs
that you love.

- [x] Blues
- [x] Reds
- [ ] Iron gall, second batch
- [ ] Shimmer inks (if the nib survives)

## Summary

$$
\bar{t} = \frac{1}{n} \sum_{i=1}^{n} t_i
$$

The averages hide more than they show: the spread within one paper was
often larger than the gap between papers. The table at the top is the
honest version.

## Appendix

Every sample was written with the same fine steel nib, cleaned between
inks with distilled water and a drop of dish soap. Room temperature stayed
between 20 and 22 °C; humidity between 40 and 55 %.

The scans live in the `scans/` folder beside this note, named by date and
ink. A few are missing where the scanner jammed; those days are marked in
the log with an asterisk.

Last line of the field notes.
