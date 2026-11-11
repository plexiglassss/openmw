return function(input)
    local cursor, output = 1, {}
    while cursor <= #input do
        local count = input[cursor]
        local indices = {}
        for i = 1, count do indices[i] = i end
        table.sort(indices, function(a,b) return input[cursor+a] < input[cursor+b] end)
        for i = 1, count do output[#output+1] = indices[i] end
        cursor = cursor + count + 1
    end
    return output
end
