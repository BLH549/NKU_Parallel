# baseline-O0
Guess time:7.88974seconds
Hash time:9.31371seconds
Train time:97.8588seconds

# baseline-O1
Guess time:0.583306seconds
Hash time:2.99998seconds
Train time:26.7262seconds

Guess time:0.561865seconds
Hash time:3.01516seconds
Train time:27.0801seconds

# baseline-O2
Guess time:0.569061seconds
Hash time:2.85628seconds
Train time:27.1128seconds

Guess time:0.576983seconds
Hash time:3.03941seconds
Train time:27.0812seconds

# 初版并行方法
## Neon O0
Guess time:7.77266seconds
Hash time:19.2691seconds
Train time:97.1503seconds

## Neon O1
Guess time:0.55748seconds
Hash time:1.82123seconds
Train time:28.2141seconds

Guess time:0.556522seconds
Hash time:1.7664seconds
Train time:27.1101seconds

Guess time:0.603394seconds
Hash time:1.93086seconds
Train time:27.4613seconds

Guess time:0.571117seconds
Hash time:1.78714seconds
Train time:26.7354seconds

===============
Guess time:0.659383seconds
Hash time:1.96416seconds
Train time:27.4388seconds

Guess time:0.647747seconds
Hash time:1.86079seconds
Train time:31.4297seconds

## Neon O2
Guess time:0.58595seconds
Hash time:1.71421seconds
Train time:27.3944seconds




# 对于原并行方法进行优化，后得到的结果

## O0
Guess time:7.65942seconds
Hash time:12.327seconds
Train time:97.1533seconds

## O1

Guess time:0.598522seconds
Hash time:1.11175seconds
Train time:27.5054seconds



## O2
Guess time:0.61635seconds
Hash time:1.04731seconds
Train time:26.8245seconds




==========================================================

# 下面的改动为之前的perf分析，不是最终优化版本的分析，仅记录
==============================================================
## Neon_simplified O1
Guess time:0.568494seconds
Hash time:2.0231seconds
Train time:27.432seconds

Guess time:0.584429seconds
Hash time:1.99899seconds
Train time:27.5439seconds

Guess time:0.559241seconds
Hash time:2.04048seconds
Train time:27.4937seconds

## Neon_simplified O2
Guess time:0.532836seconds
Hash time:1.88902seconds
Train time:27.1804seconds

Guess time:0.574168seconds
Hash time:1.88269seconds
Train time:27.1598seconds

Guess time:0.557427seconds
Hash time:1.88246seconds
Train time:26.9924seconds

## Neon_inline O1
Guess time:0.579621seconds
Hash time:2.00111seconds
Train time:26.6809seconds

## Neon_inline O2
Guess time:0.531849seconds
Hash time:1.96106seconds
Train time:26.0196seconds

## Neon_vbsl O1
Guess time:0.53616seconds
Hash time:1.83767seconds
Train time:27.2526seconds

Guess time:0.564832seconds
Hash time:1.85603seconds
Train time:27.6039seconds

## Neon_vbsl O2
Guess time:0.54256seconds
Hash time:1.83264seconds
Train time:26.6405seconds

