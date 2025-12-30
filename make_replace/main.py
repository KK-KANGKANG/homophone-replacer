import pynini
from pynini.lib import utf8, byte
from pynini import cdrewrite

sigma = utf8.VALID_UTF8_CHAR.star

rule1 = pynini.cross("dan1ni2er3bo1wei2", "丹尼尔·波维")
rule10 = pynini.cross("dan1ni2er3bo1wei4", "丹尼尔·波维")
rule2 = pynini.cross('dou4dou4', '豆豆')
rule3 = pynini.cross('zhi1dao4da2', '知道答')
rule30 = pynini.cross('chen2chen2', '橙橙')
rule4 = pynini.cross('qiao2qiao2', '峤峤')
rule5 = pynini.cross('qiu2qiu2', '球球')
rule6 = pynini.cross('lin2mei3li4', '林美丽')
rule7 = pynini.cross('guo3guo3', '果果')
rule8 = pynini.cross('miao2miao2', '苗苗')
rule9 = pynini.cross('xuan2jie4', '玄戒')
rule10 = pynini.cross('xuan2jie4xin1pian1', '玄戒芯片')
rule11 = pynini.cross('xuan2jie4xing1pian1', '玄戒芯片')
rule12 = pynini.cross('xuan2jie4xin1pian1', '玄戒芯片')
rule13 = pynini.cross('xuan2jie4xing1pian1', '玄戒芯片')


rule = (rule1 | rule10 | rule2 | rule3 | rule30 | rule4 | rule5 | rule6 | rule7 | rule8 | rule9 | rule10 | rule11 | rule12 | rule13).optimize()
rule = cdrewrite(rule, "", "", sigma)

rule.write('replace.fst')