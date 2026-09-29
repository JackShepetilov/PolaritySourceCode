import re,sys
stack=[]
for line in open(sys.argv[1],encoding='utf-16'):
    if line.lstrip().startswith('Begin Object'):
        m=re.search(r'Name="([^"]+)"',line)
        stack.append(m.group(1) if m else '?')
    elif line.strip()=='End Object':
        if stack:stack.pop()
    elif 'PropertyBindings=' in line and len(stack)>2 and stack[-3]=='AnimGraph':
        print(stack[-2],re.findall(r'PropertyName="([^"]+)"|PathAsText="([^"]+)"',line))
