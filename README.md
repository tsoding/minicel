# Minicel

The idea is to implement a batch program that can accept a CSV file that looks like this:

```csv
A      , B
1      , 2
3      , 4
=A1+B1 , =A2+B2
```

And outputs:

```csv
A      , B
1      , 2
3      , 4
3      , 7
```

Basically a simple Excel engine without any UI.
