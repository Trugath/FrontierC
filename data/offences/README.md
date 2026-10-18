# Offence parameters

Eleven rows, in the same order as the offence names in `data/text/offences/strings.tsv`.

`score` is added to the authority's offence score. `fine` is the amount the notice prints. The local fine increases by ten times that word. `threshold` is added to the law level when the police search runs; a threshold above 50 makes a found station certain.

`alert` calls the police. `message` shows the fine notice. `find_police` searches for the station that responds, and the offence is dropped when that search fails. `illegal_trade` and `bribery` record the offence and add a score, and they do not fine, notify, or call the police. Their threshold words stay in the row.

`tables.ini` keeps the original image span. The compiler writes disposable output under `build/`.
