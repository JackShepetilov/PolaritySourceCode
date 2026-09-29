import unreal
p = open('C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/LPSPAR02/target.txt', encoding='utf-8').read().strip()
g = {'__name__': '__main__'}
exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), g)
