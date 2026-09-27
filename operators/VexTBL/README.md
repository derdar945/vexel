# VexTBL 1.0.0 — small tables for Vexel

Pure C, no deps. A table is a vec of row-vecs of texts — exactly what
`csv_parse #` returns. Row 0 is the header: it stays on top through
`tbl_sort` and `tbl_pick`.

```text
@ t : csv_parse # "name,stars\njson,5\ncsv,4"
> tbl_cell # t, 2, 0              ; "csv"
> tbl_sort # t, 1                 ; numeric asc, header stays
> tbl_pick # t, 1, "5"            ; header + matching rows
```

## Verbs

```text
tbl_cols # t        max row width (number)
tbl_cell # t, r, c cell text, fail if outside
tbl_col # t, c      vec of column cells (missing = "")
tbl_sort # t, c     new table sorted asc by column, header stays;
                    numeric when the whole column parses, else byte text
tbl_pick # t, c, v  new table: header + rows where cell == v
```
